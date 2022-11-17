#pragma once

#include <chrono>
#include <thread>
#include <array>
#include <functional>

#include <fmt/format.h>

#define GRPCFY_DEFINE_LOGGING_CATEGORY(NAME, VALUE)          \
	constexpr static inline std::string_view NAME() noexcept \
	{                                                        \
		constexpr const std::string_view value{VALUE};       \
		return value;                                        \
	}

#define GRPCFY_LOG(logger, level, ...) \
	(logger).log(level, grpcfy::core::SourceLocation{__FILE__, __PRETTY_FUNCTION__, __LINE__}, __VA_ARGS__)
#define GRPCFY_TRACE(logger, ...) GRPCFY_LOG(logger, grpcfy::core::LogLevel::Trace, __VA_ARGS__)
#define GRPCFY_DEBUG(logger, ...) GRPCFY_LOG(logger, grpcfy::core::LogLevel::Debug, __VA_ARGS__)
#define GRPCFY_INFO(logger, ...) GRPCFY_LOG(logger, grpcfy::core::LogLevel::Info, __VA_ARGS__)
#define GRPCFY_WARN(logger, ...) GRPCFY_LOG(logger, grpcfy::core::LogLevel::Warning, __VA_ARGS__)
#define GRPCFY_ERROR(logger, ...) GRPCFY_LOG(logger, grpcfy::core::LogLevel::Error, __VA_ARGS__)
#define GRPCFY_FATAL(logger, ...) GRPCFY_LOG(logger, grpcfy::core::LogLevel::Fatal, __VA_ARGS__)

namespace grpcfy::core {

GRPCFY_DEFINE_LOGGING_CATEGORY(default_category, "default")

enum class LogLevel : std::uint8_t
{
	Trace,
	Debug,
	Info,
	Warning,
	Error,
	Fatal,
	__Size
};

/**
 * @brief Logger call location in source code
 */
struct SourceLocation final
{
	SourceLocation() noexcept
	    : file{nullptr}
	    , function{nullptr}
	    , line{-1}
	{
	}

	SourceLocation(const char *file, const char *function, int line) noexcept
	    : file{file}
	    , function{function}
	    , line{line}
	{
	}

	const char *file;
	const char *function;
	int line;
};

/**
 * @brief Just a message
 * @details Note, that message don't own it's category, so it's highly recommended to use functions generated
 * by GRPCFY_DEFINE_LOGGING_CATEGORY macro
 */
struct LogMessage final
{
	LogMessage() noexcept
	    : category{default_category()}
	    , level{LogLevel::__Size}
	{
	}

	LogMessage(std::string_view category,
	           LogLevel level,
	           std::chrono::system_clock::time_point timestamp,
	           std::thread::id thread_id,
	           SourceLocation location,
	           std::string &&message) noexcept
	    : category{category}
	    , level{level}
	    , timestamp{timestamp}
	    , thread_id{thread_id}
	    , location{location}
	    , message{std::move(message)}
	{
	}

	LogMessage(const LogMessage &) = default;
	LogMessage &operator=(const LogMessage &) = default;
	LogMessage(LogMessage &&) noexcept = default;
	LogMessage &operator=(LogMessage &&) noexcept = default;

	std::string_view category;
	LogLevel level;
	std::chrono::system_clock::time_point timestamp;
	std::thread::id thread_id;
	SourceLocation location;
	std::string message;
};

using LoggerCallback = std::function<void(LogMessage &&)>;
using LoggerCallbackRef = std::reference_wrapper<const LoggerCallback>;

/**
 * @brief Logger representation
 * @details Attaches category function to logging callback reference. Basic scenario it we have a single logging callback
 * per some root object, like ServiceEngine. Afterwards, this callback references are passed to  distinct objects,
 * where they joining with logging categories, forming loggers
 */
class Logger final
{
public:
	using CategoryProvider = std::string_view (*)();

public:
	Logger(CategoryProvider category_provider, LoggerCallbackRef callback_ref) noexcept
	    : category_provider{category_provider}
	    , callback_ref{callback_ref}
	{
	}

	template<typename Format, typename... Args>
	void log(LogLevel level, SourceLocation location, Format &&format, Args &&...args) const noexcept
	{
		callback_ref.get()(LogMessage{category_provider(),
		                              level,
		                              std::chrono::system_clock::now(),
		                              std::this_thread::get_id(),
		                              location,
		                              fmt::format(format, std::forward<Args>(args)...)});
	}

	[[nodiscard]] LoggerCallbackRef getCallback() const noexcept { return callback_ref; }

private:
	CategoryProvider category_provider;
	LoggerCallbackRef callback_ref;
};

}  // namespace grpcfy::core