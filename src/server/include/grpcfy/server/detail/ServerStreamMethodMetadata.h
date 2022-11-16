#pragma once

#include <grpcfy/server/ServerStreamMethod.h>

#include <google/protobuf/descriptor.h>

namespace grpcfy::server::detail {

/**
 * @brief Type erased metadata of ServerStreamMethod
 * @tparam AsyncService Service mathod belongs to
 * @details Stores userspace provided handlers, callbacks etc. Being started, initiates corresponding request processing
 */
template<typename AsyncService>
struct ServerStreamMethodMetadata
{
	using Ptr = std::unique_ptr<ServerStreamMethodMetadata>;
	virtual ~ServerStreamMethodMetadata() = default;
	virtual void makeCallHandler(core::LoggerCallbackRef logger_callback,
	                 AsyncService *async_service,
	                 grpc::ServerCompletionQueue *completion_queue) = 0;
};

template<typename AsyncService,
         typename InboundRequest,
         typename OutboundNotification,
         ServerStreamAcceptorFn<AsyncService, InboundRequest, OutboundNotification> Acceptor>
struct ServerStreamMethodMetadataImpl final : public ServerStreamMethodMetadata<AsyncService>
{
	using Self = ServerStreamMethodMetadataImpl<AsyncService, InboundRequest, OutboundNotification, Acceptor>;
	
	using Context = ServerStreamMethodContext<AsyncService, InboundRequest, OutboundNotification, Acceptor>;
	using ContextCallback = typename Context::InboundRequestCallback;

	using UserCallback = ServerStreamMethodCallback<AsyncService, InboundRequest, OutboundNotification, Acceptor>;
	using Method = ServerStreamMethod<AsyncService, InboundRequest, OutboundNotification, Acceptor>;

	explicit ServerStreamMethodMetadataImpl(const google::protobuf::MethodDescriptor *const method_descriptor,
	                                        UserCallback user_callback)
	    : method_descriptor{method_descriptor}
	    , user_provided_callback{std::move(user_callback)}
	    , inbound_request_callback{callbackFn, this}
	{
		assert(ServerStreamMethodMetadataImpl::method_descriptor);
		assert(ServerStreamMethodMetadataImpl::user_provided_callback);
	}

	void makeCallHandler(core::LoggerCallbackRef logger_callback,
	         AsyncService *async_service,
	         grpc::ServerCompletionQueue *completion_queue) final
	{
		(new Context(method_descriptor, logger_callback, async_service, completion_queue, &inbound_request_callback))
		    ->run();
	}

	const google::protobuf::MethodDescriptor *const method_descriptor;
	UserCallback user_provided_callback;
	ContextCallback inbound_request_callback;
	
private:
	static constexpr auto callbackFn = +[](Context *ctx, void *ptr) noexcept {
		const auto self = reinterpret_cast<Self *>(ptr);
		//Transfer call context ownership to userspace with unique_ptr
		self->user_provided_callback(Method{ctx->getImpl()});
	};
};

}  // namespace grpcfy::server::detail