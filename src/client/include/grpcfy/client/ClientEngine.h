#pragma once

#include <thread>
#include <typeindex>
#include <memory>
#include <future>

#include <boost/asio/thread_pool.hpp>
#include <boost/asio/post.hpp>

#include <grpcpp/grpcpp.h>

#include <grpcfy/client/Options.h>
#include <grpcfy/client/detail/SingularCallContext.h>
#include <grpcfy/client/detail/ServerStreamEntry.h>
#include <grpcfy/client/detail/ServerStreamCallContext.h>

namespace grpc {
class StubOptions;
}

// https://grpc.github.io/grpc/cpp/classgrpc_1_1_client_async_reader.html#a309d69e7e6ab1491b2e93d3716d0f680
// https://grpc.github.io/grpc/cpp/classgrpc_1_1_completion_queue.html#a86d9810ced694e50f7987ac90b9f8c1a
namespace grpcfy::client {

template<typename Stub>
using StubMakerFn = std::unique_ptr<Stub> (*)(const std::shared_ptr<grpc::ChannelInterface> &,
                                              const grpc::StubOptions &);

/**
 * @brief Client runtime engine
 *
 * @brief Dispatches RPCs to grpc runtime, and executes provided callbacks on RPC events\n
 * Has 2 internal threads - one for gRPC CompletionQueue poll, and one for asio events dispatching\n
 * All state mutations executes on asio strand, API and Completion queue events both posts to strand\n
 *
 * @tparam Stub Type of RPC stub
 * @tparam StubMaker gRPC generated stub factory function
 */
template<typename Stub, StubMakerFn<Stub> StubMaker>
class ClientEngine final
{
public:
	using Ptr = std::shared_ptr<ClientEngine>;

private:
	/**
	 * Ctor blocker, std::make_shared<T> still works
	 */
	struct Tag final
	{};

public:
	[[nodiscard]] static Ptr make(Options &&options)
	{
		return std::make_shared<ClientEngine>(Tag{}, std::move(options));
	};

	/**
	 * @brief Ctor
	 * @details Creates gRPC channel and runs internal event loop
	 * @param tag Tag to prevent stack or raw heap construction, only make() can be used
	 * @param options Default client options
	 */
	explicit ClientEngine(Tag tag, Options &&options) noexcept
	    : options{std::move(options)}
	    , channel{grpc::CreateCustomChannel(ClientEngine::options.get_address(),
	                                        ClientEngine::options.get_credentials(),
	                                        as_arguments(ClientEngine::options))}
	    , stub{StubMaker(channel, grpc::StubOptions{})}
	    , strand{runtime_context}
	    , state{ClientState::Standby}
	    , thread_pool{2}
	{
		(void)tag;

		auto run = [this]() noexcept {
			try {
				const auto work_guard = boost::asio::make_work_guard(runtime_context);
				runtime_context.run();
			} catch(const std::exception &exception) {
				std::terminate();
				//TODO lfs: handle
			} catch(...) {
				std::terminate();
			}
		};
		boost::asio::post(thread_pool, std::move(run));
	}

	/*
	 * @brief Dtor
	 * @details Shuts down both internal threads and CompletionQueue
	 */
	~ClientEngine() noexcept
	{
		std::promise<ClientState> promise;
		auto future = promise.get_future();

		auto shutdown = [this, promise = std::move(promise)]() mutable noexcept {
			state = ClientState::Standby;
			for(auto &[_, entry] : server_stream_contexts) {
				(void)_;
				entry.cancel();
			}
			promise.set_value(state);
		};
		boost::asio::post(strand, std::move(shutdown));

		const auto standby = future.get();
		(void)standby;
		assert(ClientState::Standby == standby);

		queue.Shutdown();
		runtime_context.stop();
		thread_pool.join();
	}

public:
	/**
	 * @brief Obtain current client state
	 */
	ClientState get_state() const noexcept
	{
		std::promise<ClientState> promise;
		auto future = promise.get_future();
		auto get = [this, promise = std::move(promise)]() mutable { promise.set_value(state); };
		return future.get();
	}

	/**
	 * @brief Runs internal CompletionQueue processing loop, allows API executions
	 * @details Should be called only once
	 */
	void run() noexcept
	{
		std::promise<ClientState> promise;
		auto future = promise.get_future();

		auto do_run = [this, promise = std::move(promise)]() mutable noexcept {
			if(is_running()) {
				return;
			}

			state = ClientState::Running;
			boost::asio::post(thread_pool, [this]() mutable noexcept { process_events(); });
			promise.set_value(state);
		};

		boost::asio::post(strand, std::move(do_run));

		const auto running = future.get();
		(void)running;
		assert(ClientState::Running == running);
	}

	/**
	 * @brief Executes Singular call
	 * @details Posts event to internal loop
	 * @tparam SingularCall<Request,Response,Stub,ResponseReader>  specialization
	 * @param call Call to be executed
	 */
	template<typename Call>
	void execute_singular_call(Call &&call) noexcept
	{
		auto execute = [this, call = std::forward<Call>(call)]() mutable noexcept {
			do_execute_singular_call(std::move(call));
		};
		boost::asio::post(strand, std::move(execute));
	}

	/**
	 * @brief Executes Server stream call
	 * @details Posts event to internal loop
	 * @tparam Call ServerStreamCall<Request,Response,Stub,NotificationReader>  specialization
	 * @param call  Call to be executed
	 */
	template<typename Call>
	void launch_server_stream(Call &&call) noexcept
	{
		auto launch = [this, call = std::forward<Call>(call)]() mutable noexcept {
			do_launch_server_stream(std::move(call));
		};
		boost::asio::post(strand, std::move(launch));
	}

	/**
	 * Shuts down server stream call, if present
	 * @details Posts event to internal loop
	 * @param shutdown Stream identifier to be downed
	 */
	void shutdown_server_stream(ServerStreamShutdown &&shutdown) noexcept
	{
		auto do_shutdown = [this, shutdown = std::move(shutdown)]() mutable noexcept {
			do_shutdown_server_stream(std::move(shutdown));
		};
		boost::asio::post(strand, std::move(do_shutdown));
	}

private:
	const Options options;

	const std::shared_ptr<grpc::Channel> channel;
	const std::unique_ptr<Stub> stub;
	grpc::CompletionQueue queue;

	boost::asio::io_context runtime_context;
	boost::asio::io_context::strand strand;

	ClientState state;
	boost::asio::thread_pool thread_pool;

	using ServerStreamContexts = std::map<SessionId, ServerStreamEntry>;
	ServerStreamContexts server_stream_contexts;

private:
	[[nodiscard]] bool is_running() const noexcept { return ClientState::Running == state; }

	/**
	 * @brief gRPC CompletionQueue processing loop
	 */
	void process_events() noexcept
	{
		void *tag{nullptr};
		bool ok{false};

		while(queue.Next(&tag, &ok)) {
			const auto memory_address = reinterpret_cast<CallContext::Pointer>(tag);
			const auto flags = memory_address & CallContext::kFlagsMask;
			const auto raw_context = reinterpret_cast<CallContext *>(memory_address & ~CallContext::kFlagsMask);

			auto call_context = std::unique_ptr<CallContext>(raw_context);

			auto dispatch = [this, ok, flags, call_context = std::move(call_context)]() mutable noexcept {
				const auto dead = CallContext::Aliveness::Dead == call_context->on_event(ok, state, flags);
				if(dead) {
					// It is dead, so we can destroy it
					call_context.reset(nullptr);
				} else {
					// Still alive, so we simply continue event processing its events
					(void)call_context.release();
				}
			};

			boost::asio::post(strand, std::move(dispatch));
		}
	}

private:
	template<typename Call, typename Client>
	friend class grpcfy::client::ServerStreamContext;

	template<typename Call>
	void do_execute_singular_call(Call &&call)
	{
		if(!is_running())
		{
			return;
		}

		auto call_context = std::make_unique<SingularCallContext<Call>>(
		    queue,
		    *stub,
		    std::move(call.request),
		    call.deadline ? *call.deadline : options.get_singular_call_deadline(),
		    std::move(call.callback));

		call_context.release()->run();
	}

	template<typename Call>
	void do_launch_server_stream(Call &&call)
	{
		assert(!call.session_id.empty());
		if(!is_running())
		{
			return;
		}

		const std::type_index &type_index{typeid(typename Call::Request)};

		//TODO lfs: use address of static variable of Call specialization as key to eliminate RTTI usage
		//std::uintptr_t key = reinterpret_cast< uintptr_t >( &Call::MakeReaderFn );

		const auto type_already_exists =
		    std::any_of(server_stream_contexts.begin(),
		                server_stream_contexts.end(),
		                [&type_index](const auto &pair) noexcept { return pair.second.type_index == type_index; });
		if(type_already_exists) {
			assert(false && "Duplicated stream type");
			//TODO lfs: notify via callback maybe?
			return;
		}

		const auto iterator = server_stream_contexts.lower_bound(call.session_id);
		if(iterator != server_stream_contexts.end() && iterator->first == call.session_id) {
			assert(false && "Duplicated stream id");
			//TODO lfs: notify via callback maybe?
			return;
		}

		auto session_id = call.session_id;
		auto grpc_context = std::make_shared<grpc::ClientContext>();

		server_stream_contexts.emplace_hint(
		    iterator,
		    std::piecewise_construct,
		    std::forward_as_tuple(session_id),
		    std::forward_as_tuple(
		        type_index,
		        call.session_id,
		        grpc_context,
		        strand,
		        (call.reconnect_interval ? *call.reconnect_interval : options.get_server_stream_relaunch_interval())));

		auto stream = std::make_unique<ServerStreamContext<Call, ClientEngine>>(
		    this,
		    &queue,
		    &*stub,
		    std::move(grpc_context),
		    std::move(call.request),
		    std::move(call.session_id),
		    call.deadline ? *call.deadline : options.get_server_stream_deadline(),
		    call.reconnect_policy ? *call.reconnect_policy : options.get_server_stream_relaunch_policy(),
		    std::move(call.callback));

		stream.release()->run();
	}

	void do_shutdown_server_stream(ServerStreamShutdown &&shutdown) noexcept
	{
		assert(!shutdown.session_id.empty());
		if(!is_running())
		{
			return;
		}

		const auto iterator = server_stream_contexts.find(shutdown.session_id);
		if(server_stream_contexts.end() == iterator) {
			return;
		}

		auto &[_, entry] = *iterator;
		(void)_;
		entry.cancel();
		server_stream_contexts.erase(iterator);
	}

	template<typename StreamContext>
	void relaunch_stream(std::unique_ptr<StreamContext> &&stream_context) noexcept
	{
		auto try_relaunch = [this, stream_context = std::move(stream_context)]() mutable noexcept {
			const auto iterator = server_stream_contexts.find(stream_context->session_id);
			if(server_stream_contexts.cend() == iterator) {
				assert(false);
				return;
			}

			if(ClientState::Running != state) {
				server_stream_contexts.erase(iterator);
				return;
			}

			auto &[_, entry] = *iterator;
			(void)_;
			entry.schedule_reconnect(std::move(stream_context));
		};

		boost::asio::post(strand, std::move(try_relaunch));
	}

	void cleanup_stream(const SessionId &session_id) noexcept
	{
		auto try_unregister = [this, session_id = session_id]() mutable noexcept {
			const auto iterator = server_stream_contexts.find(session_id);
			if(server_stream_contexts.cend() == iterator) {
				return;
			}

			server_stream_contexts.erase(iterator);
		};
		boost::asio::post(strand, std::move(try_unregister));
	}

	[[nodiscard]] static grpc::ChannelArguments as_arguments(const Options &options) noexcept
	{
		//https://nanxiao.me/en/message-length-setting-in-grpc/
		constexpr static auto kUnlimitedSize = -1;
		constexpr static auto kRequestSizeLimitKey = GRPC_ARG_MAX_SEND_MESSAGE_LENGTH;
		constexpr static auto kResponseSizeLimitKey = GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH;

		grpc::ChannelArguments arguments;

		arguments.SetInt(kRequestSizeLimitKey, options.get_request_size_limit_bytes().value_or(kUnlimitedSize));
		arguments.SetInt(kResponseSizeLimitKey, options.get_response_size_limit_bytes().value_or(kUnlimitedSize));

		return arguments;
	}
};

}  // namespace grpcfy::client
