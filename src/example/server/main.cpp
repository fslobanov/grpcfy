#include "GetFooHandler.h"
#include "SubscribeFooHandler.h"

#include "MemberFunctionAcceptorPrototype.h"

using namespace foobar;

void print(const grpcfy::core::LogMessage &message);

signed main(signed, char **)
{
	boost::asio::io_context io_context;
	const auto guard = boost::asio::make_work_guard(io_context);

	grpc::EnableDefaultHealthCheckService(true);

	for(const auto &method : grpcfy::core::listMethods(grpcfy::core::findService(FooBar::service_full_name()))) {
		fmt::print("Service '{}' has '{}' method, which input is '{}' and output is '{}': {}",
		           method->service()->full_name(),
		           method->full_name(),
		           method->input_type()->full_name(),
		           method->output_type()->full_name(),
		           method->DebugString());
	}

	grpc::ServerBuilder server_builder;
	grpcfy::server::Options options{FooBar::service_full_name()};
	options.addEndpoint("127.0.0.1:50505", grpc::InsecureServerCredentials())
	    .setQueueCount(2)
	    .setThreadsPerQueue(2)
	    .setHandlersPerThread(2);

	grpcfy::server::Environment environment{[&](grpcfy::core::LogMessage &&message) {
		boost::asio::post(io_context, [message = std::move(message)]() { print(message); });
	}};

	FoobarEngine foobar_engine{server_builder, std::move(options), std::move(environment)};
	const GetFooHandler get_foo_handler{foobar_engine, io_context};
	const SubscribeFooHandler subscribe_foo_handler{foobar_engine, io_context};

	const auto server = server_builder.BuildAndStart();
	assert(server && "Incorrect address maybe");

	if(const auto health_service = server->GetHealthCheckService()) {
		health_service->SetServingStatus(foobar::FooBar::service_full_name(), true);
	}

	boost::asio::steady_timer shutdown_timer{io_context};
	shutdown_timer.expires_after(20s);
	shutdown_timer.async_wait([&](boost::system::error_code ec) {
		assert(!ec);
		io_context.stop();
	});

	foobar_engine.run(server.get());
	io_context.run();

	fmt::print("Server shutdown ...\n");
	return EXIT_SUCCESS;
}

void print(const grpcfy::core::LogMessage &message)
{
	static constexpr auto format_level = +[](grpcfy::core::LogLevel level) -> std::string_view {
		using namespace grpcfy::core;
		switch(level) {
			default: return "UNKNOWN";
			case LogLevel::Trace: return "TRACE";
			case LogLevel::Debug: return "DEBUG";
			case LogLevel::Info: return "INFO";
			case LogLevel::Warning: return "WARN";
			case LogLevel::Error: return "ERROR";
			case LogLevel::Fatal: return "FATAL";
		}
	};

	static int counter = 0;
	fmt::print("[{:^5}] {:>6} {} {:<30} {} [thread:{}]\n",
	           format_level(message.level),
	           ++counter,
	           message.timestamp,
	           message.category,
	           message.message,
	           message.thread_id);
}
