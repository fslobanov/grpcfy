#include <boost/outcome.hpp>

#include <grpcfy/server/detail/ServerStreamMethodContext.h>

#pragma once

namespace grpcfy::server {

template<typename AsyncService>
class ServiceEngine;

/**
 * @brief Inbound server stream handling object, which is passed to userspace
 * @details Designed to be stored at userspace, user allowed to write multiple notifications then to close method once.
 * Additionally, method can be examined for being Running or Finished in some ways. Requires to has shorter
 * lifetime than ServiceEngine. Has a weak reference to internal method context implementation,
 * and communicates with internal FSM it via queue.
 * @tparam AsyncService Service which method belongs to
 * @tparam InboundRequest Type of inbound request
 * @tparam OutboundNotification Type of outbound notification
 * @tparam Acceptor Accepting function, see details in SingularMethodContext
 *
 * @todo Consider storing ServiceEngine * to prevent event submitting after shutdown, see CompletionQueue::Shutdown()
 */
template<typename AsyncService,
         typename InboundRequest,
         typename OutboundNotification,
         ServerStreamAcceptorFn<AsyncService, InboundRequest, OutboundNotification> Acceptor>
class ServerStreamMethod final
{
public:
	using Context = detail::ServerStreamMethodContext<AsyncService, InboundRequest, OutboundNotification, Acceptor>;
	using ContextImplPtr = typename Context::ImplPtr;

	enum class State : bool
	{
		Running,
		Finished
	};

public:
	explicit ServerStreamMethod(ContextImplPtr weak_context)
	    : weak_method_context{std::move(weak_context)}
	{
	}

public:
	/**
	 * @brief Obtain current method state
	 * @details Note, that method may be closed by remote or by userspace
	 * @return State of method
	 */
	[[nodiscard]] State getState() const noexcept
	{
		return !weak_method_context.expired() ? State::Running : State::Finished;
	}

	using GetPeer = boost::outcome_v2::result<std::string, State, boost::outcome_v2::policy::terminate>;
	/**
	 * @brief Obtain remote address
	 * @return Remote address, if method is Running yet
	 */
	[[nodiscard]] GetPeer getPeer() const noexcept
	{
		if(const auto context = weak_method_context.lock()) {
			return context->getPeer();
		}
		return State::Finished;
	}

	using GetRequest = boost::outcome_v2::result<const InboundRequest *, State, boost::outcome_v2::policy::terminate>;
	/**
	 * @brief Obtain request
	 * @return Request, if method is Running yet
	 */
	[[nodiscard]] GetRequest getRequest() const noexcept
	{
		if(const auto context = weak_method_context.lock()) {
			return &context->getRequest();
		}
		return State::Finished;
	}

public:
	/**
	 * @brief Sends notification to remote
	 * @details Drops message, is method is Finished
	 * @param notification Notification to send
	 * @return Current state of method
	 */
	State push(OutboundNotification &&notification)
	{
		const auto context = weak_method_context.lock();
		if(!context) {
			return State::Finished;
		}
		context->post(std::move(notification));
		return State::Running;
	}

	/**
	 * @brief Closes stream to remote
	 * @details Drops action, is method is Finished
	 * @param status Status to be sent
	 * @return Current state of method
	 */
	State close(grpc::Status status)
	{
		const auto method_context = weak_method_context.lock();
		if(!method_context) {
			return State::Finished;
		}
		method_context->post(std::move(status));
		return State::Running;
	}

private:
	ContextImplPtr weak_method_context;
};

/**
 * @brief Userspace callback, allows to notify userspace when requests are occurred
 */
template<typename AsyncService,
         typename InboundRequest,
         typename OutboundNotification,
         ServerStreamAcceptorFn<AsyncService, InboundRequest, OutboundNotification> Acceptor>
using ServerStreamMethodCallback =
    std::function<void(ServerStreamMethod<AsyncService, InboundRequest, OutboundNotification, Acceptor> &&)>;

}  // namespace grpcfy::server