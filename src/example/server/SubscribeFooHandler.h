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
	using Method =
	    grpcfy::server::ServerStreamMethod<FooBar::AsyncService, FooStreamRequest, FooStreamNotification, kAcceptor>;
	using Callback = grpcfy::server::
	    ServerStreamMethodCallback<FooBar::AsyncService, FooStreamRequest, FooStreamNotification, kAcceptor>;

	SubscribeFooHandler(FoobarEngine &engine, boost::asio::io_context &ctx)
	    : ctx{ctx}
	    , timer{ctx}
	    , generator{random()}
	    , distribution{0, 10}
	{
		Callback callback = [&](Method &&stream_method) { handle(std::move(stream_method)); };
		engine.registerServerStreamMethod(grpcfy::core::findMethod(FooBar::service_full_name(), "SubscribeFoo"),
		                                  std::move(callback));
		startTimer(0ms);
	}

	void handle(Method &&stream)
	{
		fmt::print("[<--] ServerStream '{}' from '{}': {}\n",
		           FooRequest::descriptor()->full_name(),
		           stream.getPeer().assume_value(),
		           stream.getRequest().assume_value()->ShortDebugString());

		boost::asio::post(
		    ctx, [&, stream = std::move(stream)]() mutable noexcept { streams.emplace_back(std::move(stream)); });
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
				    Method::State::Finished
				            == stream.close(grpc::Status{grpc::StatusCode::DO_NOT_USE, "your time is up", "no, really"})
				        ? streams.erase(iterator)
				        : std::next(iterator);
				continue;
			}

			FooStreamNotification notification;
			notification.mutable_foo()->set_value(boost::uuids::to_string(boost::uuids::random_generator{}()));

			const auto peer = stream.getPeer();
			fmt::print("[-->] ServerStream '{}' to '{}': {}\n",
			           FooResponse::descriptor()->full_name(),
			           peer ? peer.value() : std::string{"..."},
			           notification.ShortDebugString());

			iterator = Method::State::Finished == stream.push(FooStreamNotification{notification})
			               ? streams.erase(iterator)
			               : std::next(iterator);
		}

		startTimer(kTimeout);
	}

	boost::asio::io_context &ctx;
	boost::asio::steady_timer timer{ctx};
	std::vector<Method> streams;

	std::random_device random;
	std::mt19937 generator;
	std::uniform_int_distribution<std::size_t> distribution;
};

}  // namespace foobar