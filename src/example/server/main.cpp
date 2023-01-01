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

	for(const auto &method : grpcfy::core::list_methods(grpcfy::core::find_service(FooBar::service_full_name()))) {
		fmt::print("Service '{}' has '{}' method, which input is '{}' and output is '{}': {}",
		           method->service()->full_name(),
		           method->full_name(),
		           method->input_type()->full_name(),
		           method->output_type()->full_name(),
		           method->DebugString());
	}

	grpc::ServerBuilder server_builder;
	grpcfy::server::Options options{FooBar::service_full_name()};
	options.add_endpoint("127.0.0.1:50505", grpc::InsecureServerCredentials())
	    .set_queue_count(2)
	    .set_threads_per_queue(2)
	    .set_handlers_per_thread(2);

	grpcfy::server::Environment environment{[&](grpcfy::core::LogMessage &&message) {
		boost::asio::post(io_context, [message = std::move(message)]() { print(message); });
	}};

	FoobarEngine foobar_engine{server_builder, std::move(options), std::move(environment)};
	const GetFooHandler get_foo_handler{foobar_engine, io_context};
	const SubscribeFooHandler subscribe_foo_handler{foobar_engine, io_context};

	const auto server = server_builder.BuildAndStart();
	assert(server && "Incorrect address maybe");

#ifdef __linux__
	if(const auto health_service = server->GetHealthCheckService()) {
		// For unknown reason SetServingStatus fails on Mac with Exception: EXC_BAD_ACCESS (code=1, address=0x7261426f6fee)
		health_service->SetServingStatus(foobar::FooBar::service_full_name(), true);
	}
#endif

	boost::asio::steady_timer shutdown_timer{io_context};
	shutdown_timer.expires_after(20s);
	shutdown_timer.async_wait([&](boost::system::error_code ec) {
		(void)ec;
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
	fmt::print("[{:^5}] {:>6} {} {:<30} {} [thread:{}] [{}:{}]\n",
	           format_level(message.level),
	           ++counter,
	           message.timestamp,
	           message.category,
	           message.message,
	           message.thread_id,
	           message.location.file,
	           message.location.line);
}
