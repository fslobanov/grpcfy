#pragma once

#include <stdexcept>

#include <grpcpp/security/server_credentials.h>

#include <grpcfy/core/Logger.h>

namespace grpcfy::server {

/**
 * @brief ServiceEngine options
 * @details Contains addresses bind to and other engine configuration parameters
 */
class Options final
{
public:
	using Endpoints = std::map<std::string, std::shared_ptr<grpc::ServerCredentials>>;

public:
	explicit Options(std::string service_name) noexcept(false)
	    : service_name{std::move(service_name)}
	    , queue_count{1}
	    , threads_per_queue{1}
	    , handlers_per_thread{1}
	{
		if(Options::service_name.empty()) {
			throw std::invalid_argument(
			    "service name should be non empty, you could use YourGeneratedService::service_full_name() method");
		}
	}

public:
	Options &addEndpoint(const std::string &address,
	                     std::shared_ptr<grpc::ServerCredentials> credentials) noexcept(false)
	{
		if(address.empty()) {
			throw std::invalid_argument("empty address");
		}

		if(!credentials) {
			throw std::invalid_argument("null credentials");
		}

		const auto [iterator, ok] = endpoints.emplace(address, std::move(credentials));
		if(!ok) {
			throw std::invalid_argument(fmt::format("non unique address: {}", address));
		}
		(void)iterator;

		return *this;
	}

	Options &setQueueCount(std::size_t count) noexcept(false)
	{
		return setNumber<std::size_t, 1, 1024>(queue_count, count);
	}

	Options &setThreadsPerQueue(std::size_t count) noexcept(false)
	{
		return setNumber<std::size_t, 1, 1024>(threads_per_queue, count);
	}

	Options &setHandlersPerThread(std::size_t count) noexcept(false)
	{
		return setNumber<std::size_t, 1, 1024>(handlers_per_thread, count);
	}

public:
	[[nodiscard]] const std::string &getServiceName() const noexcept { return service_name; }
	[[nodiscard]] const Endpoints &getEndpoints() const noexcept { return endpoints; }

	[[nodiscard]] std::size_t getQueueCount() const noexcept { return queue_count; }
	[[nodiscard]] std::size_t getThreadsPerQueue() const noexcept { return threads_per_queue; }
	[[nodiscard]] std::size_t getHandlersPerQueue() const noexcept { return handlers_per_thread; }

private:
	std::string service_name;
	Endpoints endpoints;

	std::size_t queue_count;
	std::size_t threads_per_queue;
	std::size_t handlers_per_thread;

private:
	template<typename Number, Number Min, Number Max>
	Options &setNumber(Number &dst, Number src) noexcept(false)
	{
		if(src < Min) {
			throw std::invalid_argument("zero queues");
		}

		if(src > Max) {
			throw std::invalid_argument("are you serious?");
		}

		dst = src;
		return *this;
	}
};

/**
 * @brief Userspace provided environment, which holds callbacks, contexts etc
 */
class Environment final
{
public:
	explicit Environment(core::LoggerCallback logger_callback) noexcept(false)
	    : logger_callback{std::move(logger_callback)}
	{
		if(!Environment::logger_callback) {
			throw std::invalid_argument("null logger callback");
		}
	}

public:
	[[nodiscard]] const core::LoggerCallback &getLoggerCallback() const noexcept { return logger_callback; }

private:
	core::LoggerCallback logger_callback;
};

}  // namespace grpcfy::server