#pragma once

#include <typeindex>
#include <memory>

#include <boost/asio/strand.hpp>
#include <boost/asio/steady_timer.hpp>

#include <grpcfy/client/Common.h>

namespace grpcfy::client {

/**
 * @brief Internal client entry for server side streams pool
 */
struct ServerStreamEntry final
{
	ServerStreamEntry(std::type_index type_index,
	                  SessionId session_id,
	                  std::shared_ptr<grpc::ClientContext> context,
	                  boost::asio::io_context::strand &strand,
	                  Duration reconnect_interval) noexcept
	    : type_index(type_index)
	    , session_id(std::move(session_id))
	    , reconnect_interval(reconnect_interval)
	    , context(std::move(context))
	    , reconnect_timer(strand)
	{
	}

	~ServerStreamEntry() { cancel(); }

	void cancel() noexcept
	{
		reconnect_timer.cancel();
		context->TryCancel();
	}

	template<typename StreamContext>
	void schedule_reconnect(std::unique_ptr<StreamContext> stream_context) noexcept
	{
		context = stream_context->context;
		reconnect_timer.expires_after(reconnect_interval);
		reconnect_timer.async_wait(
		    [stream_context = std::move(stream_context)](const boost::system::error_code &ec) mutable noexcept {
			    if(boost::asio::error::operation_aborted == ec) {
				    return;
			    }
			    stream_context.release()->run();
		    });
	}

public:
	const std::type_index type_index;
	const SessionId session_id;
	const Duration reconnect_interval;

	std::shared_ptr<grpc::ClientContext> context;
	boost::asio::steady_timer reconnect_timer;
};

}  // namespace grpcfy::client