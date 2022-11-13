#pragma once
#include "Common.h"

namespace foobar {

struct GetFooHandler final
{
	constexpr static auto kAcceptor =
	    GRPCFY_SINGULAR_METHOD_ACCEPTOR(FooBar::AsyncService, FooRequest, FooResponse, RequestGetFoo);

	using Method = grpcfy::server::SingularMethod<FooBar::AsyncService, FooRequest, FooResponse, kAcceptor>;
	using Callback = grpcfy::server::SingularMethodCallback<FooBar::AsyncService, FooRequest, FooResponse, kAcceptor>;

	GetFooHandler(FoobarEngine &engine, boost::asio::io_context &ctx)
	    : ctx{ctx}
	    , counter{0}
	{
		Callback callback = [&](Method &&singular_method) { handle(std::move(singular_method)); };
		engine.registerSingularMethod(grpcfy::core::findMethod(FooBar::service_full_name(), "GetFoo"),
		                              std::move(callback));
	}

	void handle(Method &&method)
	{
		fmt::print("[<--] Singular '{}' from '{}': {}\n",
		           FooRequest::descriptor()->full_name(),
		           method.getPeer(),
		           method.getRequest().ShortDebugString());

		boost::asio::post(ctx, [&, method = std::move(method)]() mutable noexcept {
			FooResponse response;
			response.mutable_foo()->set_value(std::to_string(counter++));

			fmt::print("[-->] Singular '{}' to '{}': {}\n",
			           FooResponse::descriptor()->full_name(),
			           method.getPeer(),
			           response.ShortDebugString());

			method.respond(std::move(response));
		});
	}

	boost::asio::io_context &ctx;
	std::size_t counter;
};

}  // namespace foobar