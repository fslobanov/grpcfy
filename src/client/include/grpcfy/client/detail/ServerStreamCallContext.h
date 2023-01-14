#pragma once

#include <grpcfy/client/detail/CallContext.h>
#include <grpcfy/client/ServerStreamCall.h>

namespace grpcfy::client {

/**
 * Internal ServerStreamCall runtime
 * @tparam Call ServerStreamCall<T> specialization
 * @tparam Client ClientEngine to be run on
 */
template<typename Call, typename Client, typename EventCallback>
class ServerStreamContext final : public CallContext
{
public:
	using Stub = typename Call::Stub;
	using Request = typename Call::Request;
	using Notification = typename Call::Notification;
	using Event = typename Call::Event;

	constexpr const static auto MakeReaderFn = Call::MakeReaderFn;
	constexpr const static Flags kReadFlags = 0b1UL;

public:
	ServerStreamContext(Client *client,
	                    grpc::CompletionQueue *queue,
	                    Stub *stub,
	                    std::shared_ptr<grpc::ClientContext> context,
	                    Request &&request,
	                    SessionId session_id,
	                    Duration deadline,
	                    ServerStreamRelaunchPolicy reconnect_policy,
	                    EventCallback &&callback) noexcept
	    : client(client)
	    , request(std::move(request))
	    , queue(queue)
	    , stub(stub)
	    , context(setup_context(std::move(context), deadline))
	    , reader(((*stub).*MakeReaderFn)(&(*ServerStreamContext::context), ServerStreamContext::request, queue))
	    , session_id(std::move(session_id))
	    , deadline(deadline)
	    , state(State::Connecting)
	    , reconnect_policy(reconnect_policy)
	    , notification_buffer(std::nullopt)
	    , callback(std::forward<EventCallback>(callback))
	{
		check_flags_fit<ServerStreamContext>();

		assert(ServerStreamContext::client);
		assert(ServerStreamContext::context);
		assert(ServerStreamContext::reader);
		assert(ServerStreamContext::deadline.count() > 0);
	}

	void run() noexcept final override { reader->StartCall(tagify()); }

	Aliveness on_event(bool ok, ClientState client_state, Flags flags) noexcept final
	{
		if(!ok) {
			return on_error(client_state);
		}

		switch(state) {
			default: assert(false && "Unknown state case");
			case State::Connecting: return on_connected();
			case State::Reading: {
				assert(kReadFlags == flags);
				return on_read();
			}
			case State::Finishing: return on_finished(client_state);
		}
	};

	[[nodiscard]] static std::shared_ptr<grpc::ClientContext> setup_context(
	    std::shared_ptr<grpc::ClientContext> &&context,
	    Duration duration) noexcept
	{
		context->set_fail_fast(true);
		//context->set_deadline(to_grpc_timespec(d));
		(void)duration;
		return context;
	}

	Aliveness on_error(ClientState client_state) noexcept
	{
		state = State::Finishing;

		if(ClientState::Running == client_state) {
			reader->Finish(&status, tagify());
			return Aliveness::Alive;
		} else {
			callback({grpc::Status(grpc::StatusCode::ABORTED, "Client shutdown")});
			return Aliveness::Dead;
		}
	}

	Aliveness on_connected() noexcept
	{
		state = State::Reading;
		notification_buffer = Notification{};
		reader->Read(&*notification_buffer, tagify(kReadFlags));
		return Aliveness::Alive;
	}

	Aliveness on_read() noexcept
	{
		callback(std::move(*notification_buffer));
		notification_buffer = Notification{};
		reader->Read(&*notification_buffer, tagify(kReadFlags));
		return Aliveness::Alive;
	}

	Aliveness on_finished(ClientState client_state) noexcept
	{
		const auto should_relaunch = ClientState::Running == client_state
		                             && ServerStreamRelaunchPolicy::Relaunch == reconnect_policy
		                             && grpc::StatusCode::CANCELLED != status.error_code();

		if(!should_relaunch) {
			callback({std::move(status)});
			client->cleanup_stream(session_id);
			return Aliveness::Dead;
		}

		auto self_clone = std::make_unique<ServerStreamContext>(client,
		                                                        queue,
		                                                        stub,
		                                                        std::make_shared<grpc::ClientContext>(),
		                                                        std::move(request),
		                                                        std::move(session_id),
		                                                        deadline,
		                                                        reconnect_policy,
		                                                        std::move(callback));
		client->relaunch_stream(std::move(self_clone));
		return Aliveness::Dead;
	}

public:
	Client *const client;
	Request request;
	grpc::CompletionQueue *const queue;
	Stub *const stub;
	const std::shared_ptr<grpc::ClientContext> context;
	grpc::Status status;
	const ServerStreamReader<Notification> reader;

	SessionId session_id;
	Duration deadline;
	enum class State : uint32_t
	{
		Connecting,
		Reading,
		Finishing
	};
	State state;
	const ServerStreamRelaunchPolicy reconnect_policy;
	std::optional<Notification> notification_buffer;
	EventCallback callback;
};

}  // namespace grpcfy::client