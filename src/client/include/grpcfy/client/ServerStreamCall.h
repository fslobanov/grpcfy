#pragma once

#include <type_traits>
#include <variant>

#include <grpcpp/grpcpp.h>
//FIXME lfs: this is private header
#include <grpcpp/impl/codegen/async_stream.h>

#include <grpcfy/client/Common.h>

namespace grpcfy::client {

template<typename Response>
using ServerStreamReader = std::unique_ptr<grpc::ClientAsyncReader<Response>>;

/**
 * @brief Command for server stream shutdown
 */
struct ServerStreamShutdown final
{
	/**
	 * @brief Ctor
	 * @param session_id Stream identifier to be downed
	 */
	explicit ServerStreamShutdown(SessionId session_id)
	    : session_id(std::move(session_id))
	{
		assert(!ServerStreamShutdown::session_id.empty());
	}
	SessionId session_id;
};

/**
 * @brief Represents server-side stream RPC
 * @details Starts stream and receives stream events via Event callback\n
 * Session id should be unique within client\n
 * Each stream type session should be unique within client\n
 * May have custom deadline, reconnect interval and policy options\n
 *
 * @tparam OutboundRequest Stream initiating request
 * @tparam InboundNotification Server stream event, RPC return type
 * @tparam RpcStub Generated Stub type
 * @tparam MakeReader Stub factory function for corresponding reader creation
 */
template<typename OutboundRequest,
         typename InboundNotification,
         typename RpcStub,
         ServerStreamReader<InboundNotification> (
             RpcStub::*MakeReader)(grpc::ClientContext *, const OutboundRequest &, grpc::CompletionQueue *)>
class ServerStreamCall final
{
public:
	using Request = OutboundRequest;
	using Notification = InboundNotification;

	using Event = boost::outcome_v2::result<Notification, grpc::Status, boost::outcome_v2::policy::terminate>;
	using EventCallback = std::function<void(Event &&)>;

	using Stub = RpcStub;
	constexpr const static auto MakeReaderFn = MakeReader;

public:
	ServerStreamCall(SessionId &&session_id, Request &&request, EventCallback &&callback) noexcept
	    : session_id{std::move(session_id)}
	    , request(std::move(request))
	    , callback(std::move(callback))
	{
		assert(!ServerStreamCall::session_id.empty());
	}

	ServerStreamCall(SessionId &&session_id, EventCallback &&callback) noexcept
	    : ServerStreamCall(std::move(session_id), {}, std::move(callback))
	{
	}

	//TODO lfs: provide getter|setter and verify
	SessionId session_id;
	Request request;
	EventCallback callback;

public:
	//TODO lfs: provide getter|setter and verify
	std::optional<Duration> deadline;
	std::optional<Duration> reconnect_interval;
	std::optional<ServerStreamRelaunchPolicy> reconnect_policy;
	// more options
};

}  // namespace grpcfy::client