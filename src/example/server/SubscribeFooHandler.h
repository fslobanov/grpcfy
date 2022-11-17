#pragma once
#include "Common.h"

namespace foobar {

struct SubscribeFooHandler final
{
	constexpr static auto kTimeout = 1us;
	constexpr static auto kAcceptor = GRPCFY_SERVER_STREAM_METHOD_ACCEPTOR(FooBar::AsyncService,
	                                                                       FooStreamRequest,
	                                                                       FooStreamNotification,
	                                                                       RequestSubscribeFoo);
	using SubscribeFoo =
	    grpcfy::server::ServerStreamMethod<FooBar::AsyncService, FooStreamRequest, FooStreamNotification, kAcceptor>;
	/*using Callback = grpcfy::server::
	    ServerStreamMethodCallback<FooBar::AsyncService, FooStreamRequest, FooStreamNotification, kAcceptor>;*/

	SubscribeFooHandler(FoobarEngine &engine, boost::asio::io_context &ctx)
	    : ctx{ctx}
	    , timer{ctx}
	    , generator{random()}
	    , distribution{0, 10}
	{
		const auto descriptor = grpcfy::core::findMethod(FooBar::service_full_name(), "SubscribeFoo");
		auto callback = [this](SubscribeFoo &&subscribe_foo) { handle(std::move(subscribe_foo)); };
		engine.registerServerStreamMethod<FooStreamRequest, FooStreamNotification, kAcceptor>(descriptor,
		                                                                                      std::move(callback));
		startTimer(0ms);
	}

	void handle(SubscribeFoo &&subscribe_foo)
	{
		fmt::print("[<--] ServerStream '{}' from '{}': {}\n",
		           FooRequest::descriptor()->full_name(),
		           subscribe_foo.getPeer().assume_value(),
		           subscribe_foo.getRequest().assume_value()->ShortDebugString());

		boost::asio::post(ctx, [&, subscribe_foo = std::move(subscribe_foo)]() mutable noexcept {
			streams.emplace_back(std::move(subscribe_foo));
		});
	}

	void startTimer(std::chrono::microseconds timeout)
	{
		timer.expires_after(timeout);
		timer.async_wait([this](auto ec) { onTimer(ec); });
	}

	void onTimer(const boost::system::error_code &ec)
	{
		if(boost::asio::error::operation_aborted == ec) {
			return;
		}

		if(ec) {
			fmt::print("Timer failure: {}\n", ec.message());
			std::terminate();
		}

		for(auto iterator = streams.begin(); iterator != streams.end(); /*unused*/) {
			auto &stream = *iterator;

			if(!distribution(generator)) {
				const auto peer = stream.getPeer();
				fmt::print("[--X] ServerStream '{}' to '{}'\n",
				           FooResponse::descriptor()->full_name(),
				           peer ? peer.value() : std::string{"..."});

				iterator =
				    SubscribeFoo::State::Finished
				            == stream.close(grpc::Status{grpc::StatusCode::DO_NOT_USE, "your time is up", "no, really"})
				        ? streams.erase(iterator)
				        : std::next(iterator);
				continue;
			}

			FooStreamNotification notification;
			notification.mutable_foo()->set_value(boost::uuids::to_string(boost::uuids::random_generator{}()));

			const auto peer = stream.getPeer();
			fmt::print("[-->] ServerStream '{}' to '{}': {}\n",
			           FooStreamNotification::descriptor()->full_name(),
			           peer ? peer.value() : std::string{"..."},
			           notification.ShortDebugString());

			iterator = SubscribeFoo::State::Finished == stream.push(FooStreamNotification{notification})
			               ? streams.erase(iterator)
			               : std::next(iterator);
		}

		startTimer(kTimeout);
	}

	boost::asio::io_context &ctx;
	boost::asio::steady_timer timer{ctx};
	std::vector<SubscribeFoo> streams;

	std::random_device random;
	std::mt19937 generator;
	std::uniform_int_distribution<std::size_t> distribution;
};

}  // namespace foobar