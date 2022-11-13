#pragma once

#include <chrono>

namespace grpcfy::client {

using Duration = std::chrono::milliseconds;

/**
 * @brief Automatic broken stream relaunch or not
 */
enum class ServerStreamRelaunchPolicy : bool
{
	Relaunch,
	Shutdown
};

/**
 * @brief Unique identifier for server streams etc.
 * @details Generates internal uuid value by default
 */
using SessionId = std::string;

/**
 * @brief Remote host address client connecting to
 */
using Address = std::string;

static inline gpr_timespec toGrpcTimespec(Duration duration) noexcept
{
	const auto point = std::chrono::system_clock::now() + duration;
	gpr_timespec spec;
	grpc::Timepoint2Timespec(point, &spec);
	return spec;

	//FIXME lfs: replace system clock with monotonic
	/*using NanosCount = decltype(std::declval<gpr_timespec>().tv_nsec);

	const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
	const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(duration - seconds);

	return gpr_timespec{seconds.count(), static_cast<NanosCount>(nanoseconds.count()), GPR_TIMESPAN};*/
}

enum class ClientState : bool
{
	Running,
	Standby
};

}  // namespace grpcfy::client
