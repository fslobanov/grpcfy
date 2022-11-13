#include <fmt/format.h>

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <FooBar.grpc.pb.h>
#include <grpcfy/client/ClientEngine.h>

using namespace std::chrono_literals;

using GetFoo = grpcfy::client::SingularCall<foobar::FooRequest,
                                            foobar::FooResponse,
                                            foobar::FooBar::Stub,
                                            &foobar::FooBar::Stub::PrepareAsyncGetFoo>;

using SubscribeFoo = grpcfy::client::ServerStreamCall<foobar::FooStreamRequest,
                                                      foobar::FooStreamNotification,
                                                      foobar::FooBar::Stub,
                                                      &foobar::FooBar::Stub::PrepareAsyncSubscribeFoo>;

struct Printer final
{
	template<typename Summary>
	void printSummary(Summary &&s) noexcept
	{
		const auto &request_name = decltype(std::decay_t<decltype(s)>::request)::GetDescriptor()->full_name();

		if(s.result) {
			fmt::print("[<--] Singular '{}' OK: request - '{}', response - '{}'\n",
			           request_name,
			           s.request.ShortDebugString(),
			           s.result.value().ShortDebugString());
		} else {
			fmt::print("[<--] Singular '{}' FAIL: request - '{}', message - '{}', detail - '{}'\n",
			           request_name,
			           s.request.ShortDebugString(),
			           s.result.error().error_message(),
			           s.result.error().error_details());
		}
	}

	template<typename Event>
	void printEvent(Event &&e) noexcept
	{
		const auto &notification_name = std::decay_t<decltype(e)>::value_type::GetDescriptor()->full_name();
		if(e) {
			fmt::print("[<--] Server stream '{}' OK: notify - '{}'\n", notification_name, e.value().ShortDebugString());
		} else {
			fmt::print("[<--] Server stream '{}' FAIL: message - '{}', detail - '{}'\n",
			           notification_name,
			           e.error().error_message(),
			           e.error().error_details());
		}
	}

	void operator()(GetFoo::Summary &&s) noexcept { printSummary(std::move(s)); }
	void operator()(SubscribeFoo::Event &&e) noexcept { printEvent(std::move(e)); }
};

signed main(signed, char **)
{
	grpcfy::client::Options options{"127.0.0.1:50505"};
	options.setSingularCallDeadline(1s);
	options.setServerStreamRelaunchPolicy(grpcfy::client::ServerStreamRelaunchPolicy::Relaunch);
	options.setServerStreamRelaunchInterval(100ms);

	const auto client =
	    grpcfy::client::ClientEngine<foobar::FooBar::Stub, &foobar::FooBar::NewStub>::make(std::move(options));
	client->run();

	{
		foobar::FooStreamRequest request;
		request.set_value(boost::uuids::to_string(boost::uuids::random_generator{}()));
		client->launchServerStream(
		    SubscribeFoo{grpcfy::client::SessionId{"foo-interested"}, std::move(request), Printer{}});
	}

	for(auto count = 0UL; count < 1000; ++count) {
		foobar::FooRequest request;
		request.set_value(boost::uuids::to_string(boost::uuids::random_generator{}()));
		client->executeSingularCall(GetFoo{std::move(request), Printer{}});
	}

	std::this_thread::sleep_for(10s);
}