#pragma once

#include "Common.h"

namespace foobar {

template<typename Srv,
         typename Req,
         typename Res,
         void (Srv::*Acceptor)(grpc::ServerContext *,
                               Req *,
                               grpc::ServerAsyncResponseWriter<Res> *,
                               grpc::CompletionQueue *,
                               grpc::ServerCompletionQueue *,
                               void *)>
struct ServiceSingularMemberFunctionTest final
{
	void operator()(Srv &srv) { (srv.*Acceptor)(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr); }
};

template<typename Srv,
         typename Req,
         typename Not,
         void (Srv::*Acceptor)(grpc::ServerContext *,
                               Req *,
                               grpc::ServerAsyncWriter<Not> *,
                               grpc::CompletionQueue *,
                               grpc::ServerCompletionQueue *,
                               void *)>
struct ServiceStreamMemberFunctionTest final
{
	void operator()(Srv &srv) { (srv.*Acceptor)(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr); }
};

void member_function_acceptor_prototype()
{
	// clang-format off
	// same as grpc generated code
	using MyAsyncService =
		FooBar::WithAsyncMethod_GetFoo<
	    FooBar::WithAsyncMethod_GetBar<
	    FooBar::WithAsyncMethod_SubscribeFoo<
	    FooBar::WithAsyncMethod_SubscribeBar<
	    FooBar::WithAsyncMethod_StreamFoo<
	    FooBar::WithAsyncMethod_StreamBar<
	    FooBar::WithAsyncMethod_ExchangeFooBar<FooBar::Service>>>>>>>;

	static_assert(std::is_same_v<MyAsyncService, FooBar::AsyncService>,
	    "Mismatch with codegen, check .proto file, method order matters");

	FooBar::AsyncService service;

	ServiceSingularMemberFunctionTest<
	    MyAsyncService,
		FooRequest,
		FooResponse,
		&FooBar::AsyncService::RequestGetFoo>
	{}(service);

	// this won't compile, because it's not MyAsyncService method
	/*ServiceStreamMemberFunctionTest<
		MyAsyncService,
		FooStreamRequest,
		FooStreamNotification,
		&FooBar::AsyncService::RequestSubscribeFoo>
	{}(service);*/

	// this won't compile, because reinterpret_cast is not constexpr
	/*ServiceStreamMemberFunctionTest<
		MyAsyncService,
		FooStreamRequest,
		FooStreamNotification,
		reinterpret_cast<void (MyAsyncService::*)(grpc::ServerContext *,
				   FooStreamRequest *,
				   grpc::ServerAsyncWriter<FooStreamNotification> *,
				   grpc::CompletionQueue *,
				   grpc::ServerCompletionQueue *,
				   void *)>(&FooBar::AsyncService::RequestSubscribeFoo)>
	{}(service);*/

	// this works, but what a cost
	// user had to unroll service alias to required method type, method insertion|addition|removal breaks everything
	using MySubscribeFooService =
		FooBar::WithAsyncMethod_SubscribeFoo<
		FooBar::WithAsyncMethod_SubscribeBar<
		FooBar::WithAsyncMethod_StreamFoo<
		FooBar::WithAsyncMethod_StreamBar<
		FooBar::WithAsyncMethod_ExchangeFooBar<FooBar::Service>>>>>;

	ServiceStreamMemberFunctionTest<
	    MySubscribeFooService,
	    FooStreamRequest,
	    FooStreamNotification,
	    &FooBar::AsyncService::RequestSubscribeFoo>
	{}(service);

	// clang-format on
}
}  // namespace foobar