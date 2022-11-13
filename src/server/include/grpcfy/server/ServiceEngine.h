#pragma once

#include <map>
#include <memory>

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
	    , completion_queue{initialize(builder)}
	    , thread_pool{1}
	{
	}

	~ServiceEngine() noexcept
	{
		completion_queue->Shutdown();

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

		if(singular_calls.empty() && server_stream_calls.empty()) {
			throw std::runtime_error("none of calls registered");
		}

		boost::asio::post(thread_pool, [this]() noexcept {
			auto thread_name = options.getServiceName();
			thread_name.resize(15);
			pthread_setname_np(pthread_self(), thread_name.c_str());

			runAll();

			try {
				processingLoop();
			} catch(const std::exception &exception) {
				GRPCFY_FATAL(logger, "Unexpected exception occurred: {}", exception.what());
			}
		});
	}

	/**
	 * @brief Register SingularMethod handler, should be unique for each Service method
	 * @tparam InboundRequest Type of inbound request
	 * @tparam OutboundResponse Type of outbound response
	 * @tparam Acceptor Accepting function, see details in SingularMethodContext
	 * @param method_descriptor Reflective method descriptor
	 * @param on_request New call notification callback
	 */
	template<typename InboundRequest,
	         typename OutboundResponse,
	         SingularMethodAcceptorFn<AsyncService, InboundRequest, OutboundResponse> Acceptor>
	void registerSingularMethod(
	    MethodDescriptorPtr method_descriptor,
	    SingularMethodCallback<AsyncService, InboundRequest, OutboundResponse, Acceptor> &&on_request) noexcept(false)
	{
		checkMessageDerived<InboundRequest>();
		checkMessageDerived<OutboundResponse>();
		checkNullArguments(method_descriptor, on_request);
		checkDescriptorsMatch<InboundRequest>(method_descriptor->input_type());
		checkDescriptorsMatch<OutboundResponse>(method_descriptor->output_type());

		auto metadata = std::make_unique<
		    detail::SingularMethodMetadataImpl<AsyncService, InboundRequest, OutboundResponse, Acceptor>>(
		    method_descriptor, std::move(on_request));

		const auto [iterator, ok] = singular_calls.emplace(method_descriptor, std::move(metadata));
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
	         ServerStreamAcceptorFn<AsyncService, InboundRequest, OutboundNotification> Acceptor>
	void registerServerStreamMethod(
	    MethodDescriptorPtr method_descriptor,
	    ServerStreamMethodCallback<AsyncService, InboundRequest, OutboundNotification, Acceptor>
	        &&on_request) noexcept(false)
	{
		checkMessageDerived<InboundRequest>();
		checkMessageDerived<OutboundNotification>();
		checkNullArguments(method_descriptor, on_request);
		checkDescriptorsMatch<InboundRequest>(method_descriptor->input_type());
		checkDescriptorsMatch<OutboundNotification>(method_descriptor->output_type());

		auto metadata = std::make_unique<
		    detail::ServerStreamMethodMetadataImpl<AsyncService, InboundRequest, OutboundNotification, Acceptor>>(
		    method_descriptor, std::move(on_request));

		const auto [iterator, ok] = server_stream_calls.emplace(method_descriptor, std::move(metadata));
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
	const Options options;
	const Environment environment;

	const grpcfy::core::Logger logger;

	AsyncService async_service;
	std::unique_ptr<grpc::ServerCompletionQueue> completion_queue;

	boost::asio::thread_pool thread_pool;

	std::map<MethodDescriptorPtr, typename detail::SingularMethodMetadata<AsyncService>::Ptr> singular_calls;
	std::map<MethodDescriptorPtr, typename detail::ServerStreamMethodMetadata<AsyncService>::Ptr> server_stream_calls;

private:
	std::unique_ptr<grpc::ServerCompletionQueue> initialize(grpc::ServerBuilder &builder) noexcept
	{
		for(const auto &[address, credentials] : options.getEndpoints()) {
			builder.AddListeningPort(address, credentials);
		}
		return builder.RegisterService(&async_service).AddCompletionQueue(true);
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
	void runAll()
	{
		assert(completion_queue);

		for(auto &[key, metadata] : singular_calls) {
			(void)key;
			metadata->run(environment.getLoggerCallback(), &async_service, completion_queue.get());
		}

		for(auto &[key, metadata] : server_stream_calls) {
			(void)key;
			metadata->run(environment.getLoggerCallback(), &async_service, completion_queue.get());
		}
	}

	void processingLoop()
	{
		using namespace detail;
		void *tag{nullptr};
		bool ok{false};
		while(completion_queue->Next(&tag, &ok)) {
			const auto memory_address = reinterpret_cast<MethodContext::Pointer>(tag);
			const auto flags = memory_address & MethodContext::kFlagsMask;
			const auto call_context = reinterpret_cast<MethodContext *>(memory_address & ~MethodContext::kFlagsMask);
			GRPCFY_DEBUG(logger, "Got tag - {}, flags - {:#02x}, ok - {}", fmt::ptr(call_context), flags, ok);
			call_context->onEvent(ok, flags);
		}
	};
};

}  // namespace grpcfy::server