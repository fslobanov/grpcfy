#pragma once

#include <ratio>
#include <optional>
#include <utility>

#include <grpcpp/grpcpp.h>

#include <grpcfy/client/Common.h>

namespace grpcfy::client {

/**
 * @brief Options to configure ClientEngine
 * @details Provides default timeout|policy values, which can be overridden for each call individually
 */
class [[nodiscard]] Options final
{
public:
	using Creadentials = std::shared_ptr<grpc::ChannelCredentials>;
	using SizeLimitBytes = std::optional<int>;

public:
	/**
	 * @brief Ctor
	 * @param address Remote host address to connect
	 * @throws std::invalid_argument if address is invalid
	 */
	explicit Options(Address address) noexcept(false)
	    : address(std::move(address))
	    , credentials(grpc::InsecureChannelCredentials())
	    , singular_call_deadline{std::chrono::seconds{1}}
	    , server_stream_deadline{std::chrono::seconds{1}}
	    , server_stream_relaunch_interval{std::chrono::seconds{5}}
	    , server_stream_relaunch_policy{ServerStreamRelaunchPolicy::Relaunch}
	    , request_size_limit_bytes(std::mega::num * 32)   ///< default request size limit is 32Mb
	    , response_size_limit_bytes(std::mega::num * 32)  ///< default response size limit is 32Mb
	{
		if(Options::address.empty()) {
			throw std::invalid_argument("empty address");
		}
	}

public:
	/**
	 * @brief Obtains remote host address
	 */
	[[nodiscard]] const Address &get_address() const noexcept { return address; }

	/**
	 * @brief Obtains gRPC channel credentials
	 * @return
	 */
	[[nodiscard]] const Creadentials &get_credentials() const noexcept { return credentials; }
	/**
	 * @brief Sets gRPC channel credentials
	 * @param creds Credentials to be set
	 * @return Chaining ref
	 * @throws std::invalid_argument if creds is null
	 */
	Options &set_credentials(Creadentials &&creds) noexcept(false)
	{
		if(!credentials) {
			throw std::invalid_argument("null credentials");
		}
		credentials = std::move(creds);
		return *this;
	}

	/**
	 * @brief Obtains singular call deadline
	 * @return Default deadline value
	 */
	[[nodiscard]] Duration get_singular_call_deadline() const noexcept { return singular_call_deadline; }
	/**
	 * @brief Sets default SingularCall deadline
	 * @param deadline Deadline to be set
	 * @return Chaining ref
	 * @throws std::invalid_argument if deadline is less than 10 msec
	 */
	Options &set_singular_call_deadline(Duration deadline) noexcept(false)
	{
		if(deadline.count() < 10) {
			throw std::invalid_argument("invalid call deadline, should be greater than 10 msec");
		}
		singular_call_deadline = deadline;
		return *this;
	}

	/**
	 * @brief Obtains server stream call deadline
	 * @return Default deadline value
	 */
	[[nodiscard]] Duration get_server_stream_deadline() const noexcept { return server_stream_deadline; }
	/**
	 * @brief Sets default ServerStreamCall deadline
	 * @param deadline Deadline to be set
	 * @return Chaining ref
	 * @throws std::invalid_argument if deadline is less than 10 msec
	 */
	Options &set_server_stream_deadline(Duration deadline) noexcept(false)
	{
		if(deadline.count() < 10) {
			throw std::invalid_argument("invalid call deadline, should be greater than 10 msec");
		}
		server_stream_deadline = deadline;
		return *this;
	}

	/**
	 * @brief Obtains server stream relaunch interval
	 * @return Default interval value
	 */
	[[nodiscard]] Duration get_server_stream_relaunch_interval() const noexcept { return server_stream_relaunch_interval; }
	/**
	 * @brief Sets default ServerStreamCall relaunch interval
	 * @param interval Interval to be set
	 * @return Chaining ref
	 * @throws std::invalid_argument if interval is less than 100 msec
	 */
	Options &set_server_stream_relaunch_interval(Duration interval) noexcept(false)
	{
		if(interval.count() < 100) {
			throw std::invalid_argument("invalid relaunch interval, should be greater than 100 msec");
		}
		server_stream_relaunch_interval = interval;
		return *this;
	}

	/**
	 * @brief Obtains server stream call relaunch policy
	 * @return Default policy value
	 */
	[[nodiscard]] ServerStreamRelaunchPolicy get_server_stream_relaunch_policy() const noexcept
	{
		return server_stream_relaunch_policy;
	}
	/**
	 * @brief Sets default stream relaunch policy
	 * @param policy Policy to be set
	 * @return Chaining ref
	 */
	Options &set_server_stream_relaunch_policy(ServerStreamRelaunchPolicy policy) noexcept
	{
		server_stream_relaunch_policy = policy;
		return *this;
	}

	/**
	 * @brief Obtains client request size limit in bytes
	 * @return Empty, if no limit is set
	 */
	[[nodiscard]] SizeLimitBytes get_request_size_limit_bytes() const noexcept { return request_size_limit_bytes; }

	/**
	 * @brief Sets default request size limit in bytes
	 * @param limit Limit to set, may be empty to be unlimited
	 * @return Chaining ref
	 */
	Options &set_request_size_limit_bytes(SizeLimitBytes limit)
	{
		if(limit && *limit <= 0) {
			throw std::invalid_argument("limit should be positive: " + std::to_string(*limit));
		}

		request_size_limit_bytes = limit;
		return *this;
	}

	/**
	 * @brief Obtains client response size limit in bytes
	 * @return Empty, if no limit is set
	 */
	[[nodiscard]] SizeLimitBytes get_response_size_limit_bytes() const noexcept { return response_size_limit_bytes; }

	/**
	 * @brief Sets default response size limit in bytes
	 * @param limit Limit to set, may be empty to be unlimited
	 * @return Chaining ref
	 */
	Options &set_response_size_limit_bytes(SizeLimitBytes limit)
	{
		if(limit && *limit <= 0) {
			throw std::invalid_argument("limit should be positive: " + std::to_string(*limit));
		}

		request_size_limit_bytes = limit;
		return *this;
	}

private:
	Address address;
	std::shared_ptr<grpc::ChannelCredentials> credentials;

	Duration singular_call_deadline;

	Duration server_stream_deadline;
	Duration server_stream_relaunch_interval;
	ServerStreamRelaunchPolicy server_stream_relaunch_policy;

	SizeLimitBytes request_size_limit_bytes;
	SizeLimitBytes response_size_limit_bytes;
};

}  // namespace grpcfy::client