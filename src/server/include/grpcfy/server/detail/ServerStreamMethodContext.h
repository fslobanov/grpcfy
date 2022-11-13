#pragma once

#include <queue>

#include <boost/outcome.hpp>

#include <grpcpp/grpcpp.h>
#include <grpcpp/alarm.h>
//Workaround for older grpc versions
#include <grpcpp/impl/codegen/async_stream.h>

#include <grpcfy/server/detail/MethodContext.h>

#define GRPCFY_SERVER_STREAM_METHOD_ACCEPTOR(SERVICE_TYPE, REQUEST_TYPE, STREAM_TYPE, ACCEPT_METHOD) \
	+[](SERVICE_TYPE *service,                                                                       \
	    ::grpc::ServerContext *context,                                                              \
	    REQUEST_TYPE *request,                                                                       \
	    ::grpc::ServerAsyncWriter<STREAM_TYPE> *stream_writer,                                       \
	    ::grpc::CompletionQueue *new_call_cq,                                                        \
	    ::grpc::ServerCompletionQueue *notification_cq,                                              \
	    void *tag) { service->ACCEPT_METHOD(context, request, stream_writer, new_call_cq, notification_cq, tag); }

namespace grpcfy::server {
/**
 * @brief ServerStreamMethod acceptor signature, see below for description
 */
template<typename AsyncService, typename InboundRequest, typename OutboundNotification>
using ServerStreamAcceptorFn = void (*)(AsyncService *,
                                        grpc::ServerContext *,
                                        InboundRequest *,
                                        grpc::ServerAsyncWriter<OutboundNotification> *,
                                        grpc::CompletionQueue *,
                                        grpc::ServerCompletionQueue *,
                                        void *);
}  // namespace grpcfy::server

namespace grpcfy::server::detail {

GRPCFY_DEFINE_LOGGING_CATEGORY(server_stream_context_category, "ServerStreamMethodContext");

/**
 * @brief Finite State Machine of ServerStreamMethod
 * @tparam AsyncService Service which method belongs to
 * @tparam InboundRequest Type of inbound request
 * @tparam OutboundNotification Type of outbound response
 * @tparam Acceptor  Accepting function, see SingularMethodContext for details
 *
 * @details Has internal implementation, which is shared with userspace via weak pointer. Userspace communicates
 * with context via queue of events, stored in implementation
 */
template<typename AsyncService,
         typename InboundRequest,
         typename OutboundNotification,
         ServerStreamAcceptorFn<AsyncService, InboundRequest, OutboundNotification> Acceptor>
class ServerStreamMethodContext final : public MethodContext
{
public:
	using Self = ServerStreamMethodContext<AsyncService, InboundRequest, OutboundNotification, Acceptor>;
	using InboundRequestCallback = std::function<void(Self *)>;

	class Impl;
	using ImplPtr = std::weak_ptr<Impl>;

public:
	ServerStreamMethodContext(const google::protobuf::MethodDescriptor *method_descriptor,
	                          core::LoggerCallbackRef logger_callback,
	                          AsyncService *service,
	                          grpc::ServerCompletionQueue *completion_queue,
	                          const InboundRequestCallback *inbound_request_callback) noexcept
	    : impl{std::make_shared<Impl>(this,
	                                  method_descriptor,
	                                  logger_callback,
	                                  service,
	                                  completion_queue,
	                                  inbound_request_callback)}
	{
		checkFlagsFit<Self>();
		GRPCFY_DEBUG(impl->getLogger(), "{} constructed", impl->identity());
	}

	~ServerStreamMethodContext() final { GRPCFY_DEBUG(impl->getLogger(), "{} destructed", impl->identity()); }

public:
	/**
	 * @brief Obtain type of this method
	 */
	[[nodiscard]] Type getType() const noexcept final { return Type::ServerStream; }
	/**
	 * @brief Start method execution
	 */
	void run() noexcept final { impl->run(); }
	/**
	 * @brief Handle event received from CompletionQueue
	 * @param ok Success of event
	 * @param flags Pointer tags
	 */
	void onEvent(bool ok, Flags flags) noexcept final { impl->onEvent(ok, flags); }
	/**
	 * @brief Obtain internal implementation weak reference, to share it with userspace
	 */
	[[nodiscard]] ImplPtr getImpl() const noexcept { return impl; }

private:
	enum class State : uint32_t
	{
		StandingBy,             //< Initial state, nothing happens
		AwaitingRequest,        ///< Wait for next inbound request
		AwaitingNotifications,  ///< Wait for notifications from userspace
		AwaitingAlarm,          ///< After userspace notifies, wait for event from CompletionQueue
		AwaitingWrite,          ///< Awaiting notification message being written
		AwaitingFinish,         ///< Awaiting stream being finished, destroying this method when done

		Cancelled  ///< Method is cancelled by remote via AsyncNotifyWhenDone
	};

	enum class Tag : Pointer
	{
		AsyncNotifyWhenDone = 1
	};

public:
	class Impl final
	{
	public:
		using NotificationOneOf =
		    boost::outcome_v2::result<OutboundNotification, grpc::Status, boost::outcome_v2::policy::terminate>;

	public:
		Impl(Self *context,
		     const google::protobuf::MethodDescriptor *method_descriptor,
		     core::LoggerCallbackRef logger_callback,
		     AsyncService *service,
		     grpc::ServerCompletionQueue *completion_queue,
		     const InboundRequestCallback *inbound_request_callback) noexcept
		    : context{context}
		    , method_descriptor{method_descriptor}
		    , logger{server_stream_context_category, logger_callback}
		    , inbound_request_callback{inbound_request_callback}
		    , service{service}
		    , completion_queue{completion_queue}
		    , stream_writer{&server_context}
		    , state{State::StandingBy}
		    , alarm_count{0}
		    , drop_notifications{false}
		{
			assert(context);
			assert(method_descriptor);
			assert(logger_callback.get());
			assert(service);
			assert(completion_queue);
			assert(inbound_request_callback);

			server_context.AsyncNotifyWhenDone(context->tagify(static_cast<Pointer>(Tag::AsyncNotifyWhenDone)));
		}

	public:
		[[nodiscard]] std::string getPeer() const { return server_context.peer(); }
		[[nodiscard]] const InboundRequest &getRequest() const noexcept { return inbound_request; }

		void post(NotificationOneOf &&notification)
		{
			const std::scoped_lock lock{mutex};
			GRPCFY_DEBUG(logger, "{} userspace posts, state - {}", identity(), toString(state));

			if(drop_notifications) {
				GRPCFY_DEBUG(logger, "{} dropped", identity());
				return;
			}

			if(!notification) {
				drop_notifications = true;
				GRPCFY_DEBUG(logger, "{} closed by userspace", identity());
			}

			switch(state) {
				default: assert(false && "unknown state");

				case State::StandingBy: [[fallthrough]];
				case State::AwaitingRequest: [[fallthrough]];
				case State::AwaitingFinish: {
					assert(false && "illegal state");
					return;
				}

				case State::AwaitingAlarm: [[fallthrough]];
				case State::AwaitingWrite: {
					GRPCFY_DEBUG(logger, "{} is processing notifications, pushing", identity());
					notifications_queue.push(std::move(notification));
				} break;

				case State::AwaitingNotifications: {
					GRPCFY_DEBUG(logger, "{} is waiting for notifications, alarming + pushing", identity());
					state = State::AwaitingAlarm;
					++alarm_count;
					notifications_queue.push(std::move(notification));
					notification_alarm.Set(completion_queue, core::rightNow(), context->tagify());
				} break;

				case State::Cancelled: {
					GRPCFY_DEBUG(logger, "{} notification attempt after cancellation", identity());
					break;
				}
			}
		}

	private:
		Self *const context;
		const google::protobuf::MethodDescriptor *const method_descriptor;
		const core::Logger logger;
		const InboundRequestCallback *const inbound_request_callback;

		AsyncService *const service;

		grpc::ServerCompletionQueue *const completion_queue;
		grpc::ServerContext server_context;
		grpc::Alarm notification_alarm;
		grpc::ServerAsyncWriter<OutboundNotification> stream_writer;

		InboundRequest inbound_request;

		std::mutex mutex;
		State state;
		std::queue<NotificationOneOf> notifications_queue;
		std::size_t alarm_count;
		bool drop_notifications;

	public:
		std::string identity() const noexcept
		{
			return fmt::format(
			    "{}[method:{},impl:{}]", method_descriptor->full_name(), fmt::ptr(context), fmt::ptr(this));
		}

		const core::Logger &getLogger() const noexcept { return logger; }

		void run()
		{
			assert(State::StandingBy == state && "illegal state");
			GRPCFY_DEBUG(logger, "{} running", identity());

			state = State::AwaitingRequest;
			(*Acceptor)(service,
			            &server_context,
			            &inbound_request,
			            &stream_writer,
			            completion_queue,
			            completion_queue,
			            context->tagify());
		}

		void onEvent(bool ok, Flags flags) noexcept
		{
			const std::scoped_lock lock{mutex};
			GRPCFY_DEBUG(logger,
			             "{} got event, state - {}, ok - {}, flags - {:#02x}, queue - {}, alarms - {}",
			             identity(),
			             toString(state),
			             ok,
			             flags.to_ulong(),
			             notifications_queue.size(),
			             alarm_count);

			if(!ok) {
				GRPCFY_WARN(logger,
				            "{} not ok, suicide, queue - {}, alarms - {}",
				            identity(),
				            notifications_queue.size(),
				            alarm_count);

				context->suicide();
				return;
			}

			if(server_context.IsCancelled()) {
				GRPCFY_DEBUG(logger, "{} cancelled by remote", identity());
				state = State::Cancelled;
				drop_notifications = true;
			}

			switch(state) {
				default: assert(false && "unknown state"); break;

				case State::AwaitingNotifications: [[fallthrough]];
				case State::StandingBy: {
					assert(false && "illegal state");
				} break;

				case State::AwaitingRequest: onRequest(flags); break;
				case State::AwaitingAlarm: onAlarm(flags); break;
				case State::AwaitingWrite: onWrite(flags); break;
				case State::AwaitingFinish: onFinished(flags); break;
				case State::Cancelled: onCancelled(flags); break;
			}
		}

	private:
		void onRequest(Flags)
		{
			GRPCFY_DEBUG(logger, "{} notifying userspace", identity());
			(new Self(method_descriptor, logger.getCallback(), service, completion_queue, inbound_request_callback))
			    ->run();
			state = State::AwaitingNotifications;
			(*inbound_request_callback)(context);
		}

		void onAlarm(Flags)
		{
			assert(!notifications_queue.empty());
			assert(alarm_count >= 1);

			--alarm_count;
			processPendingNotification();
		}

		void onWrite(Flags)
		{
			if(notifications_queue.empty()) {
				GRPCFY_DEBUG(logger, "{} awaiting notification", identity());
				state = State::AwaitingNotifications;
				return;
			}
			processPendingNotification();
		}

		void onCancelled(Flags)
		{
			//This is last pending alarm, we can destroy itself
			if(alarm_count <= 1) {
				GRPCFY_DEBUG(logger, "{} destroying on cancel", identity());
				context->suicide();
				return;
			}

			//There are pending alarms in completion queue due to high concurrency, so we had to drain them one by one
			--alarm_count;
			notifications_queue = {};
			GRPCFY_DEBUG(logger, "{} draining pending alarm on cancel, alarms - {}", identity(), alarm_count);
		}

		void onFinished(Flags flags)
		{
			if(static_cast<Pointer>(Tag::AsyncNotifyWhenDone) == flags.to_ullong()) {
				GRPCFY_DEBUG(logger,
				             "{} got AsyncNotifyWhenDoneTag - {}, alarms - {}",
				             identity(),
				             notifications_queue.size(),
				             alarm_count);
				return;
			}

			GRPCFY_DEBUG(logger,
			             "{} finished, destructing, queue - {}, alarms - {}",
			             identity(),
			             notifications_queue.size(),
			             alarm_count);

			context->suicide();
		}

		void processPendingNotification()
		{
			assert(!notifications_queue.empty());

			auto notification = std::move(notifications_queue.front());
			notifications_queue.pop();

			if(notification) {
				GRPCFY_DEBUG(
				    logger, "{} writing, queue - {}, alarms - {}", identity(), notifications_queue.size(), alarm_count);

				state = State::AwaitingWrite;
				stream_writer.Write(std::move(notification.value()), context->tagify());
			} else {
				GRPCFY_DEBUG(logger,
				             "{} finishing, queue - {}, alarms - {}",
				             identity(),
				             notifications_queue.size(),
				             alarm_count);

				state = State::AwaitingFinish;
				stream_writer.Finish(std::move(notification.error()), context->tagify());
			}
		}
	};

private:
	const std::shared_ptr<Impl> impl;

private:
	static constexpr std::string_view toString(State state) noexcept
	{
		switch(state) {
			default: return "Unknown";
			case State::StandingBy: return "StandingBy";
			case State::AwaitingRequest: return "AwaitingRequest";
			case State::AwaitingNotifications: return "AwaitingNotifications";
			case State::AwaitingAlarm: return "AwaitingAlarm";
			case State::AwaitingWrite: return "AwaitingWrite";
			case State::Cancelled: return "Cancelled";
			case State::AwaitingFinish: return "AwaitingFinish";
		}
	}
};
}  // namespace grpcfy::server::detail