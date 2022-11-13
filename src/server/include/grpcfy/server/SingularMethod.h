#pragma once

#include <grpcfy/server/detail/SingularMethodContext.h>

namespace grpcfy::server {

template<typename AsyncService>
class ServiceEngine;

/**
 * @brief Inbound singular request handling object, which is passed to userspace
 * @details Designed to be passed to userspace, user allowed to respond once. Requires to has shorter
 * lifetime than ServiceEngine. Being passed to userspace, takes ownership to internal method context.
 * Being responded, transfers method ownership to method itself.
 * @tparam AsyncService Service which method belongs to
 * @tparam InboundRequest Type of inbound request
 * @tparam OutboundResponse Type of outbound response
 * @tparam Acceptor Accepting function, see details in SingularCallContext
 *
 * @todo Consider storing ServiceEngine * to prevent event submitting after shutdown, see CompletionQueue::Shutdown()
 */
template<typename AsyncService,
         typename InboundRequest,
         typename OutboundResponse,
         SingularMethodAcceptorFn<AsyncService, InboundRequest, OutboundResponse> Acceptor>
class SingularMethod final
{
public:
	using Context = detail::SingularMethodContext<AsyncService, InboundRequest, OutboundResponse, Acceptor>;
	using ContextPtr = std::unique_ptr<Context>;
	using ResponseOneOf = typename Context::ResponseOneOf;

	using RequestType = InboundRequest;
	using ResponseType = OutboundResponse;

public:
	explicit SingularMethod(ContextPtr method_context) noexcept
	    : method_context{std::move(method_context)}
	{
	}

public:
	/**
	 * @brief Obtain remote address
	 */
	[[nodiscard]] std::string getPeer() const { return method_context->getPeer(); }
	/**
	 * @brief Obtain request
	 */
	[[nodiscard]] const InboundRequest &getRequest() const noexcept { return method_context->getRequest(); }
	/**
	 * @brief Release request
	 */
	[[nodiscard]] InboundRequest &&releaseRequest() noexcept { return method_context->releaseRequest(); }

public:
	/**
	 * @brief Sends response to remote
	 * @details May be positive or negative, allowed to be executed only once
	 * @param notification Response to send
	 */
	void respond(ResponseOneOf &&outbound_response)
	{
		assert(method_context);
		//Transfer method context ownership to server completion queue and respond
		method_context.release()->respond(std::move(outbound_response));
	}

private:
	std::unique_ptr<Context> method_context;
};

/**
 * @brief Userspace callback, allows to notify userspace when requests are occurred
 */
template<typename AsyncService,
         typename InboundRequest,
         typename OutboundResponse,
         SingularMethodAcceptorFn<AsyncService, InboundRequest, OutboundResponse> Acceptor>
using SingularMethodCallback =
    std::function<void(SingularMethod<AsyncService, InboundRequest, OutboundResponse, Acceptor> &&)>;

}  // namespace grpcfy::server