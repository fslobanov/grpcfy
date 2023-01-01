#pragma once
#include "Common.h"

namespace foobar {

struct GetFooHandler final
{
	// Acceptor function which obtains RPCs from gRPC service runtime
	constexpr static auto kAcceptor =
	    GRPCFY_SINGULAR_METHOD_ACCEPTOR(FooBar::AsyncService, FooRequest, FooResponse, RequestGetFoo);

	// Inbound call method signature
	using GetFoo = grpcfy::server::SingularMethod<FooBar::AsyncService, FooRequest, FooResponse, kAcceptor>;
	// This alias being provided by engine, so take care of capture size
	//using Callback = grpcfy::server::SingularMethodCallback<FooBar::AsyncService, FooRequest, FooResponse, kAcceptor>;

	GetFooHandler(FoobarEngine &engine, boost::asio::io_context &ctx)
	    : ctx{ctx}
	    , counter{0}
	{
		// First, we need to obtain method descriptor from database - be vigilant, method should exist etc
		const auto descriptor = grpcfy::core::find_method(FooBar::service_full_name(), "GetFoo");
		// Callback, which proceeds inbound calls, callback should satisfy Callable concept
		// and accept only 1 argument - inbound call instance
		// Note that you may use Callback alias described above, callback declared as template parameter
		// to avoid std::function usage
		auto callback = [this](GetFoo &&get_foo) noexcept { handle(std::move(get_foo)); };
		// Simply register this callback as inbound call handler
		engine.register_singular_method<FooRequest, FooResponse, kAcceptor>(descriptor, std::move(callback));
	}

	void handle(GetFoo &&get_foo) noexcept
	try {
		fmt::print("[<--] Singular '{}' from '{}': {}\n",
		           FooRequest::descriptor()->full_name(),
		           get_foo.get_peer(),
		           get_foo.get_request().ShortDebugString());

		// Transfer call event from Engine thread to App context
		boost::asio::post(ctx, [&, get_foo = std::move(get_foo)]() mutable noexcept {
			// Do a bit of serious business, we are on App threads already
			FooResponse response;
			response.mutable_foo()->set_value(std::to_string(counter++));

			fmt::print("[-->] Singular '{}' to '{}': {}\n",
			           FooResponse::descriptor()->full_name(),
			           get_foo.get_peer(),
			           response.ShortDebugString());

			// No need to synchronize App response with Engine threads, call context covers that problem itself
			get_foo.respond(std::move(response));
		});

	} catch(...) {  // You have to handle your exceptions yourself, nobody cares
		fmt::print("You touch my talala");
	}

	boost::asio::io_context &ctx;
	std::size_t counter;
};

}  // namespace foobar