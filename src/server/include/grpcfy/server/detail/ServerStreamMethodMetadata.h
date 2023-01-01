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
	virtual void spawn(core::LoggerCallbackRef logger_callback,
	                   AsyncService *async_service,
	                   grpc::ServerCompletionQueue *completion_queue) = 0;
};

template<typename Self, typename Context, typename Method>
void serverStreamMethodMetadataImplCallback(Context *__restrict__ ctx, void *__restrict__ ptr)
{
	// We assume that Self if is ServerStreamMethodMetadataImpl
	const auto self = reinterpret_cast<Self *>(ptr);
	Method method{ctx->getImpl()};
	self->user_callback(std::move(method));
}

template<typename AsyncService,
         typename InboundRequest,
         typename OutboundNotification,
         ServerStreamAcceptorFn<AsyncService, InboundRequest, OutboundNotification> Acceptor,
         typename UserCallback>
struct ServerStreamMethodMetadataImpl final : public ServerStreamMethodMetadata<AsyncService>
{
	using Self =
	    ServerStreamMethodMetadataImpl<AsyncService, InboundRequest, OutboundNotification, Acceptor, UserCallback>;

	using Context = ServerStreamMethodContext<AsyncService, InboundRequest, OutboundNotification, Acceptor>;
	using ContextCallback = typename Context::InboundRequestCallback;

	//using UserCallback = ServerStreamMethodCallback<AsyncService, InboundRequest, OutboundNotification, Acceptor>;
	using Method = ServerStreamMethod<AsyncService, InboundRequest, OutboundNotification, Acceptor>;

	explicit ServerStreamMethodMetadataImpl(const google::protobuf::MethodDescriptor *const method_descriptor,
	                                        UserCallback user_callback)
	    : method_descriptor{method_descriptor}
	    , user_callback{std::forward<UserCallback>(user_callback)}
	    , inbound_request_callback{serverStreamMethodMetadataImplCallback<Self, Context, Method>, this}
	{
		assert(ServerStreamMethodMetadataImpl::method_descriptor);
		//assert(ServerStreamMethodMetadataImpl::user_provided_callback);
	}

	void spawn(core::LoggerCallbackRef logger_callback,
	           AsyncService *async_service,
	           grpc::ServerCompletionQueue *completion_queue) final
	{
		auto context =
		    new Context(method_descriptor, logger_callback, async_service, completion_queue, &inbound_request_callback);
		context->run();
	}

	const google::protobuf::MethodDescriptor *const method_descriptor;
	UserCallback user_callback;
	ContextCallback inbound_request_callback;
};

}  // namespace grpcfy::server::detail