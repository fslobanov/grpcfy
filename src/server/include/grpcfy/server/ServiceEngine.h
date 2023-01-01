#pragma once

#include <map>
#include <memory>

#include <pthread.h>

#include <boost/asio/thread_pool.hpp>
#include <boost/asio/post.hpp>

#include <grpcfy/core/Grpc.h>
#include <grpcfy/server/Configuration.h>
#include <grpcfy/server/detail/SingularMethodMetadata.h>
#include <grpcfy/server/detail/ServerStreamMethodMetadata.h>

namespace grpcfy::server {

GRPCFY_DEFINE_LOGGING_CATEGORY(service_engine_category, "ServiceEngine");

/**
 * @brief gRPC service execution engine
 * @tparam AsyncService Service to execute
 * @details Attaches owned service to ServerBuilder, then runs Service on Server. Allows to register Service
 * call handlers, handlers should be unique for each call. Does primary validations of passed data, throws an
 * exceptions on errors, be careful. Owns Service and CompletionQueue, but not Server, it's an userspace duty
 * to shutdown server manually
 */
template<typename AsyncService>
class ServiceEngine final
{
public:
	using MethodDescriptorPtr = const google::protobuf::MethodDescriptor *;

public:
	/**
	 * @brief Ctor
	 * @details Accepts grpc::ServerBuilder as construction parameter in order to run multiple Engines on single server.
	 * @param builder Builder to register service
	 * @param options Engine options
	 * @param environment Engine environment
	 */
	explicit ServiceEngine(grpc::ServerBuilder &builder, Options options, Environment environment) noexcept(false)
	    : options{std::move(options)}
	    , environment{std::move(environment)}
	    , logger{service_engine_category, ServiceEngine::environment.getLoggerCallback()}
	    , completion_queues{initialize(builder)}
	    , thread_pool{options.getQueueCount() * options.getThreadsPerQueue()}
	{
	}

	~ServiceEngine() noexcept
	{
		for(auto &queue : completion_queues) {
			queue->Shutdown();
		}

		thread_pool.stop();
		thread_pool.join();
	}

public:
	/**
	 * @brief Runs Service execution, should be called only once
	 * @param server Requirement for the server to be built and running
	 */
	void run(grpc::Server *server) noexcept(false)
	{
		if(!server) {
			throw std::runtime_error("null server, check your builder or address configuration");
		}

		GRPCFY_INFO(logger, "Running '{}' service on: {}", options.getServiceName(), [this]() {
			std::string message;

			for(const auto &[address, credentials] : options.getEndpoints()) {
				(void)credentials;
				message += fmt::format("{},", address);
			}

			if(!message.empty()) {
				message.pop_back();
			}
			return message;
		}());

		if(singular_methods.empty() && server_stream_methods.empty()) {
			throw std::runtime_error("none of calls registered");
		}

		for(auto &queue : completion_queues) {
			assert(queue);

			const auto worker = [this, queue = queue.get()]() noexcept {
				auto thread_name = options.getServiceName();
				thread_name.resize(15);

#ifdef __linux__
				pthread_setname_np(pthread_self(), thread_name.c_str());
#elif __APPLE__
				pthread_setname_np(thread_name.c_str());
#else
#error "not supported"
#endif

				setupQueue(queue);

				try {
					processQueue(queue);
				} catch(const std::exception &exception) {
					//TODO lfs: handle, shutdown pool and notify for example
					GRPCFY_FATAL(logger, "Unexpected exception occurred: {}", exception.what());
				}
			};

			for(auto count{0u}; count < options.getThreadsPerQueue(); ++count) {
				boost::asio::post(thread_pool, worker);
			}
		}
	}

	/**
	 * @brief Register SingularMethod handler, should be unique for each Service method
	 * @tparam InboundRequest Type of inbound request
	 * @tparam OutboundResponse Type of outbound response
	 * @tparam Acceptor Accepting function, see details in SingularMethodContext
	 * @param method_descriptor Reflective method descriptor
	 * @param on_request New call notification callback
	 */

	template<
	    typename InboundRequest,
	    typename OutboundResponse,
	    //SingularMethodCallback<AsyncService, InboundRequest, OutboundResponse, Acceptor> &&user_callback) noexcept(false)
	    SingularMethodAcceptorFn<AsyncService, InboundRequest, OutboundResponse> Acceptor,
	    typename UserCallback>
	void registerSingularMethod(MethodDescriptorPtr method_descriptor, UserCallback &&user_callback) noexcept(false)
	{
		checkMessageDerived<InboundRequest>();
		checkMessageDerived<OutboundResponse>();
		//checkNullArguments(method_descriptor, user_callback);
		checkDescriptorsMatch<InboundRequest>(method_descriptor->input_type());
		checkDescriptorsMatch<OutboundResponse>(method_descriptor->output_type());

		auto metadata = std::make_unique<
		    detail::SingularMethodMetadataImpl<AsyncService, InboundRequest, OutboundResponse, Acceptor, UserCallback>>(
		    method_descriptor, std::forward<UserCallback>(user_callback));

		const auto [iterator, ok] = singular_methods.emplace(method_descriptor, std::move(metadata));
		(void)iterator;

		if(!ok) {
			throw std::invalid_argument(fmt::format("duplicated singular call: {}", method_descriptor->full_name()));
		}

		GRPCFY_INFO(logger,
		            "Service '{}' method '{}' register succeed",
		            options.getServiceName(),
		            method_descriptor->full_name());
	}

	/**
	 * @brief Register ServerStreamMethod handler, should be unique for each Service method
	 * @tparam InboundRequest Type of inbound request
	 * @tparam OutboundNotification Type of outbound notification
	 * @tparam Acceptor Accepting function, see details in SingularMethodContext
	 * @param method_descriptor Reflective method descriptor
	 * @param on_request New call notification callback
	 */
	template<typename InboundRequest,
	         typename OutboundNotification,
	         ServerStreamAcceptorFn<AsyncService, InboundRequest, OutboundNotification> Acceptor,
	         typename UserCallback>
	void registerServerStreamMethod(
	    MethodDescriptorPtr method_descriptor,
	    //ServerStreamMethodCallback<AsyncService, InboundRequest, OutboundNotification, Acceptor> &&on_request) noexcept(false)
	    UserCallback &&user_callback)
	{
		checkMessageDerived<InboundRequest>();
		checkMessageDerived<OutboundNotification>();
		//checkNullArguments(method_descriptor, user_callback);
		checkDescriptorsMatch<InboundRequest>(method_descriptor->input_type());
		checkDescriptorsMatch<OutboundNotification>(method_descriptor->output_type());

		auto metadata = std::make_unique<detail::ServerStreamMethodMetadataImpl<AsyncService,
		                                                                        InboundRequest,
		                                                                        OutboundNotification,
		                                                                        Acceptor,
		                                                                        UserCallback>>(
		    method_descriptor, std::forward<UserCallback>(user_callback));

		const auto [iterator, ok] = server_stream_methods.emplace(method_descriptor, std::move(metadata));
		(void)iterator;

		if(!ok) {
			throw std::invalid_argument(
			    fmt::format("duplicated server stream call: {}", method_descriptor->full_name()));
		}

		GRPCFY_INFO(logger,
		            "Service '{}' method '{}' register succeed",
		            options.getServiceName(),
		            method_descriptor->full_name());
	}

private:
	using CompletionQueues = std::vector<std::unique_ptr<grpc::ServerCompletionQueue>>;

private:
	const Options options;
	const Environment environment;

	const grpcfy::core::Logger logger;

	AsyncService async_service;
	CompletionQueues completion_queues;

	boost::asio::thread_pool thread_pool;

	std::map<MethodDescriptorPtr, typename detail::SingularMethodMetadata<AsyncService>::Ptr> singular_methods;
	std::map<MethodDescriptorPtr, typename detail::ServerStreamMethodMetadata<AsyncService>::Ptr> server_stream_methods;

private:
	CompletionQueues initialize(grpc::ServerBuilder &builder) noexcept
	{
		builder.RegisterService(&async_service);

		for(const auto &[address, credentials] : options.getEndpoints()) {
			builder.AddListeningPort(address, credentials);
		}

		CompletionQueues queues;
		queues.resize(options.getQueueCount());
		std::generate(queues.begin(), queues.end(), [&builder]() { return builder.AddCompletionQueue(true); });

		return queues;
	}

private:
	template<typename T>
	constexpr static void checkMessageDerived() noexcept
	{
		static_assert(std::is_base_of_v<google::protobuf::Message, T>, "Should be derived from protobuf message");
	}

	template<typename Callback>
	void checkNullArguments(MethodDescriptorPtr method_descriptor, const Callback &callback) const noexcept(false)
	{
		if(!method_descriptor) {
			throw std::invalid_argument("null descriptor");
		}

		if(!callback) {
			throw std::invalid_argument("null callback");
		}
	}

	template<typename T>
	void checkDescriptorsMatch(const google::protobuf::Descriptor *descriptor) const noexcept(false)
	{
		//Should be same pointers, google::protobuf::Descriptor has no comparison operators
		if(T::descriptor() != descriptor) {
			throw std::invalid_argument(
			    fmt::format("descriptors mismatch: {} and {}", T::descriptor()->full_name(), descriptor->full_name()));
		}
	}

private:
	void setupQueue(grpc::ServerCompletionQueue *queue)
	{
		assert(!completion_queues.empty());

		for(auto &[key, metadata] : singular_methods) {
			(void)key;

			for(auto count{0u}; count < options.getHandlersPerQueue(); ++count) {
				metadata->spawn(environment.getLoggerCallback(), &async_service, queue);
			}
		}

		for(auto &[key, metadata] : server_stream_methods) {
			(void)key;

			for(auto count{0u}; count < options.getHandlersPerQueue(); ++count) {
				metadata->spawn(environment.getLoggerCallback(), &async_service, queue);
			}
		}
	}

	void processQueue(grpc::ServerCompletionQueue *queue)
	{
		using namespace detail;

		void *tag{nullptr};
		bool ok{false};

		while(queue->Next(&tag, &ok)) {
			const auto memory_address = reinterpret_cast<MethodContext::Pointer>(tag);
			const auto flags = memory_address & MethodContext::kFlagsMask;
			const auto context = reinterpret_cast<MethodContext *>(memory_address & ~MethodContext::kFlagsMask);

			GRPCFY_DEBUG(logger, "Got tag - {}, flags - {:#02x}, ok - {}", fmt::ptr(context), flags, ok);

			context->onEvent(ok, flags);
		}
	};
};

}  // namespace grpcfy::server