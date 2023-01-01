#pragma once

#include <grpcfy/server/SingularMethod.h>

namespace grpcfy::server::detail {

/**
 * @brief Type erased metadata of SingularMethod
 * @tparam AsyncService Service call belongs to
 * @details Stores userspace provided handlers, callbacks etc. Being started, initiates corresponding request processing
 */
template<typename AsyncService>
struct SingularMethodMetadata
{
	using Ptr = std::unique_ptr<SingularMethodMetadata>;
	virtual ~SingularMethodMetadata() = default;
	virtual void spawn(core::LoggerCallbackRef logger_callback,
	                   AsyncService *async_service,
	                   grpc::ServerCompletionQueue *completion_queue) = 0;
};

template<typename Self, typename Context, typename Method>
void singularMethodMetadataImplCallback(Context *__restrict__ ctx, void *__restrict__ ptr)
{
	// We assume that Self if is SingularMethodMetadataImpl
	const auto self = reinterpret_cast<Self *>(ptr);
	//Transfer call context ownership to userspace with unique_ptr
	Method method{std::unique_ptr<Context>{ctx}};
	self->user_callback(std::move(method));
}

template<typename AsyncService,
         typename InboundRequest,
         typename OutboundResponse,
         SingularMethodAcceptorFn<AsyncService, InboundRequest, OutboundResponse> Acceptor,
         typename UserCallback>
struct SingularMethodMetadataImpl final : public SingularMethodMetadata<AsyncService>
{
	using Self = SingularMethodMetadataImpl<AsyncService, InboundRequest, OutboundResponse, Acceptor, UserCallback>;

	using Context = SingularMethodContext<AsyncService, InboundRequest, OutboundResponse, Acceptor>;
	using ContextCallback = typename Context::InboundRequestCallback;

	//using UserCallback = SingularMethodCallback<AsyncService, InboundRequest, OutboundResponse, Acceptor>;
	using Method = SingularMethod<AsyncService, InboundRequest, OutboundResponse, Acceptor>;

	explicit SingularMethodMetadataImpl(const google::protobuf::MethodDescriptor *const method_descriptor,
	                                    UserCallback &&user_callback)
	    : method_descriptor{method_descriptor}
	    , user_callback{std::forward<UserCallback>(user_callback)}
	    , inbound_request_callback{singularMethodMetadataImplCallback<Self, Context, Method>, this}
	{
		assert(SingularMethodMetadataImpl::method_descriptor);
		//assert(SingularMethodMetadataImpl::user_callback);
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
	const UserCallback user_callback;
	const ContextCallback inbound_request_callback;
};

}  // namespace grpcfy::server::detail
