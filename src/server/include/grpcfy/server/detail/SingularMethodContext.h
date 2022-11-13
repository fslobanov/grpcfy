#pragma once

#include <optional>

#include <boost/outcome.hpp>

#include <grpcpp/grpcpp.h>
#include <grpcpp/alarm.h>
//Workaround for older grpc versions
#include <grpcpp/impl/codegen/async_unary_call.h>

#include <grpcfy/core/Logger.h>
#include <grpcfy/server/detail/MethodContext.h>

#define GRPCFY_SINGULAR_METHOD_ACCEPTOR(SERVICE_TYPE, REQUEST_TYPE, RESPONSE_TYPE, ACCEPT_METHOD)     \
	+[](SERVICE_TYPE *service,                                                                        \
	    ::grpc::ServerContext *context,                                                               \
	    REQUEST_TYPE *request,                                                                        \
	    ::grpc::ServerAsyncResponseWriter<RESPONSE_TYPE> *response_writer,                            \
	    ::grpc::CompletionQueue *new_call_cq,                                                         \
	    ::grpc::ServerCompletionQueue *notification_cq,                                               \
	    void *tag) {                                                                                  \
		assert(service &&context &&request &&response_writer &&new_call_cq &&notification_cq &&tag);  \
		service->ACCEPT_METHOD(context, request, response_writer, new_call_cq, notification_cq, tag); \
	}

#define GRPCFY_SINGULAR_METHOD_ACCEPTOR_FN(FN, SERVICE_TYPE, REQUEST_TYPE, RESPONSE_TYPE, ACCEPT_METHOD) \
	inline void FN(SERVICE_TYPE *service,                                                                \
	               ::grpc::ServerContext *context,                                                       \
	               REQUEST_TYPE *request,                                                                \
	               ::grpc::ServerAsyncResponseWriter<RESPONSE_TYPE> *response_writer,                    \
	               ::grpc::CompletionQueue *new_call_cq,                                                 \
	               ::grpc::ServerCompletionQueue *notification_cq,                                       \
	               void *tag)                                                                            \
	{                                                                                                    \
		assert(service &&context &&request &&response_writer &&new_call_cq &&notification_cq &&tag);     \
		service->ACCEPT_METHOD(context, request, response_writer, new_call_cq, notification_cq, tag);    \
	}

namespace grpcfy::server {
/**
 * @brief SingularMethod acceptor signature, see below for description
 */
template<typename AsyncService, typename InboundRequest, typename OutboundResponse>
using SingularMethodAcceptorFn = void (*)(AsyncService *,
                                          grpc::ServerContext *,
                                          InboundRequest *,
                                          grpc::ServerAsyncResponseWriter<OutboundResponse> *,
                                          grpc::CompletionQueue *,
                                          grpc::ServerCompletionQueue *,
                                          void *);
}  // namespace grpcfy::server

namespace grpcfy::server::detail {

GRPCFY_DEFINE_LOGGING_CATEGORY(singular_method_category, "SingularMethodContext");

/**
 * @brief Finite State Machine of SingularMethod
 * @tparam AsyncService Service which method belongs to
 * @tparam InboundRequest  Type of inbound request
 * @tparam OutboundResponse Type of outbound response
 * @tparam Acceptor  Accepting function
 *
 * @details Why we are using some strange function like Acceptor, when we can just pass Service
 * member function pointer? That's because gRPC generated code is a bullshit, AsyncService is NOT a type,
 * it's an alias for CRTP CHAIN OF ALL SERVICE CALLS, which breaks on method insertion|removal|addition:
 * typedef  WithAsyncMethod_GetFoo<
 *          WithAsyncMethod_GetBar<
 *          WithAsyncMethod_SubscribeFoo<
 *          WithAsyncMethod_SubscribeBar<
 *          WithAsyncMethod_StreamFoo<
 *          WithAsyncMethod_StreamBar<
 *          WithAsyncMethod_ExchangeFooBar<Service > > > > > > > AsyncService;
 * Due to this, we can't pass AsyncService::*SomeAsyncRpcMethod, because this requires to specify
 * proper nested type, that's insane, look at server example application to see concept. Another solution is to
 * pass Acceptor as functor type, which would method required Service methods, but we will loose function constraints,
 * so this way leads to incomprehensible template compilation errors. So i decided to ask user code to pass
 * free Acceptor function pointer, which accepts Service pointer as first argument, and calls corresponding RPC method
 * internally. Feel free to use this Acceptor function generation macros, or use positive lambdas to cast anonymous
 * functor to free function pointer, like '+[](Service *, grpc::ServerContext ... ){ service->RealRpcMethodHere(...); }'
 */
template<typename AsyncService,
         typename InboundRequest,
         typename OutboundResponse,
         SingularMethodAcceptorFn<AsyncService, InboundRequest, OutboundResponse> Acceptor>
class SingularMethodContext final : public MethodContext
{
public:
	using Self = SingularMethodContext<AsyncService, InboundRequest, OutboundResponse, Acceptor>;
	using InboundRequestCallback = std::function<void(Self *)>;

	using ResponseOneOf =
	    boost::outcome_v2::result<OutboundResponse, grpc::Status, boost::outcome_v2::policy::terminate>;

public:
	SingularMethodContext(const google::protobuf::MethodDescriptor *method_descriptor,
	                      core::LoggerCallbackRef logger_callback,
	                      AsyncService *service,
	                      grpc::ServerCompletionQueue *completion_queue,
	                      const InboundRequestCallback *inbound_request_callback) noexcept
	    : method_descriptor{method_descriptor}
	    , logger{singular_method_category, logger_callback}
	    , service{service}
	    , completion_queue{completion_queue}
	    , response_writer{&server_context}
	    , state{State::StandingBy}
	    , inbound_request_callback{inbound_request_callback}
	{
		checkFlagsFit<Self>();

		assert(service);
		assert(completion_queue);
		assert(inbound_request_callback);

		GRPCFY_DEBUG(logger, "{} constructed", identity());

		//TODO lfs: use server_context.AsyncNotifyWhenDone to prevent unnecessary response if outdated
	}

	~SingularMethodContext() final { GRPCFY_DEBUG(logger, "{} destructed", identity()); }

public:
	/**
	 * @brief Obtain type of this method
	 */
	Type getType() const noexcept override { return Type::Singular; }

	/**
	 * @brief Start method execution
	 */
	void run() noexcept override
	{
		assert(State::StandingBy == state && "illegal state");
		GRPCFY_DEBUG(logger, "{} running", identity());

		state = State::AwaitingRequest;
		(*Acceptor)(service,
		            &server_context,
		            &inbound_request,
		            &response_writer,
		            completion_queue,
		            completion_queue,
		            tagify(static_cast<Pointer>(state)));
	}

	/**
	 * @brief Handle event received from CompletionQueue
	 * @param ok Success of event
	 * @param flags Pointer tags
	 */
	void onEvent(bool ok, Flags flags) noexcept override
	{
		GRPCFY_DEBUG(logger,
		             "{} got event, state - {}, ok - {}, flags - {:#02x}",
		             identity(),
		             toString(state),
		             ok,
		             flags.to_ulong());

		if(!ok) {
			GRPCFY_WARN(logger, "{} not ok, suicide", identity());
			suicide();
			return;
		}

		//TODO lfs: take into account server_context.IsCancelled()

		switch(state) {
			default: assert(false && "unknown state"); break;

			case State::StandingBy: [[fallthrough]];
			case State::AwaitingResponse: {
				assert(false && "illegal state");
			} break;

			case State::AwaitingRequest: onRequest(flags); break;
			case State::AwaitingAlarm: onAlarm(flags); break;
			case State::AwaitingFinish: onFinished(flags); break;
		}
	}

public:
	/**
	 * @brief Obtains remote address
	 */
	[[nodiscard]] std::string getPeer() const { return server_context.peer(); }
	/**
	 * @brief Obtains request
	 */
	[[nodiscard]] const InboundRequest &getRequest() const noexcept { return inbound_request; }
	/**
	 * @brief Releases request
	 */
	[[nodiscard]] InboundRequest &&releaseRequest() noexcept { return std::move(inbound_request); }

	/**
	 * @brief Writes a response, received from userspace
	 * @details Notifies gRPC runtime via CompletionQueue event
	 */
	void respond(ResponseOneOf &&response)
	{
		assert(State::AwaitingResponse == state);
		GRPCFY_DEBUG(logger, "{} userspace responds, state - {}", identity(), toString(state));

		state = State::AwaitingAlarm;
		outbound_response = std::move(response);

		processed_alarm.Set(completion_queue, core::rightNow(), tagify(static_cast<Pointer>(state)));
	}

private:
	enum class State : Pointer
	{
		StandingBy = 42UL,         ///< Initial state, nothing happens
		AwaitingRequest = 0b01UL,  ///< Wait for next inbound request, tagged
		AwaitingResponse = 666UL,  ///< Request passed to userspace, waiting request being processed
		AwaitingAlarm = 0b10UL,    ///< After userspace responds, wait for event from CompletionQueue, tagged
		AwaitingFinish = 0b11UL    ///< Awaiting response being written, destroying this method when done, tagged
	};

private:
	const google::protobuf::MethodDescriptor *const method_descriptor;
	const core::Logger logger;

	AsyncService *const service;

	grpc::ServerCompletionQueue *const completion_queue;
	grpc::ServerContext server_context;
	grpc::Alarm processed_alarm;
	grpc::ServerAsyncResponseWriter<OutboundResponse> response_writer;

	InboundRequest inbound_request;
	std::optional<ResponseOneOf> outbound_response;

	State state;
	const InboundRequestCallback *const inbound_request_callback;

private:
	void onRequest(Flags flags)
	{
		assert(static_cast<Pointer>(State::AwaitingRequest) == flags.to_ulong());
		GRPCFY_DEBUG(logger, "{} notifying userspace", identity());

		(new Self(method_descriptor, logger.getCallback(), service, completion_queue, inbound_request_callback))->run();

		state = State::AwaitingResponse;
		(*inbound_request_callback)(this);
	}

	void onAlarm(Flags flags)
	{
		assert(static_cast<Pointer>(State::AwaitingAlarm) == flags.to_ulong());
		state = State::AwaitingFinish;

		assert(outbound_response);
		auto &response = *outbound_response;

		if(response) {
			GRPCFY_DEBUG(logger, "{} writing", identity());
			response_writer.Finish(response.value(), grpc::Status::OK, tagify(static_cast<Pointer>(state)));
		} else {
			GRPCFY_DEBUG(logger, "{} finishing", identity());
			response_writer.Finish({}, response.error(), tagify(static_cast<Pointer>(state)));
		}
	}

	void onFinished(Flags flags)
	{
		assert(static_cast<Pointer>(State::AwaitingFinish) == flags.to_ulong());
		GRPCFY_DEBUG(logger, "{} finished, destructing", identity());
		suicide();
	}

private:
	std::string identity() const noexcept
	{
		return fmt::format("{}[{}]", method_descriptor->full_name(), fmt::ptr(this));
	}

	static constexpr std::string_view toString(State state) noexcept
	{
		switch(state) {
			default: return "Unknown";
			case State::StandingBy: return "StandingBy";
			case State::AwaitingRequest: return "AwaitingRequest";
			case State::AwaitingResponse: return "AwaitingResponse";
			case State::AwaitingAlarm: return "AwaitingAlarm";
			case State::AwaitingFinish: return "AwaitingFinish";
		}
	}
};

}  // namespace grpcfy::server::detail
