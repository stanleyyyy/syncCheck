#pragma once
#include <string>
#include <sstream>
#include <cassert>

enum LogLevel {
	LOG_FATAL = 0,
	LOG_ERROR,
	LOG_WARNING,
	LOG_INFO,
	LOG_DEBUG,
	LOG_TRACE
};

class Logger {
public:
	static Logger& getInstance();
	void setLogLevel(LogLevel level);
	LogLevel getLogLevel() const;
	bool isEnabledFor(LogLevel level) const;
	void logMessage(LogLevel level, const std::string& message, const bool inlineMode = false);
private:
	Logger();
	Logger(const Logger&) = delete;
	Logger& operator=(const Logger&) = delete;
	LogLevel m_logLevel;
	static constexpr size_t MAX_MSG_LENGTH = 1024;
};

class LogBuffer {
public:
	// The third parameter 'inlineMode' indicates if header/newline should be omitted.
	LogBuffer(Logger& logger, LogLevel level, bool inlineMode = false)
		: m_logger(logger)
		, m_level(level)
		, m_inline(inlineMode)
	{}
	~LogBuffer() {
		if (m_logger.isEnabledFor(m_level)) {
			m_logger.logMessage(m_level, m_stream.str(), m_inline);
		}
	}
	template<typename T>
	LogBuffer& operator<<(const T& value) {
		m_stream << value;
		return *this;
	}
private:
	Logger& m_logger;
	LogLevel m_level;
	bool m_inline;
	std::ostringstream m_stream;
};

#define LOG_STREAM(level)			LogBuffer(Logger::getInstance(), level)
#define LOG_INLINE_STREAM(level)	LogBuffer(Logger::getInstance(), level, true)

#define DBG_FATAL(...)			do { if (Logger::getInstance().isEnabledFor(LOG_FATAL))			LOG_STREAM(LOG_FATAL) << __VA_ARGS__; } while(0)
#define DBG_ERROR(...)			do { if (Logger::getInstance().isEnabledFor(LOG_ERROR))			LOG_STREAM(LOG_ERROR) << __VA_ARGS__; } while(0)
#define DBG_WARN(...)			do { if (Logger::getInstance().isEnabledFor(LOG_WARNING))		LOG_STREAM(LOG_WARNING) << __VA_ARGS__; } while(0)
#define DBG_INFO(...)			do { if (Logger::getInstance().isEnabledFor(LOG_INFO))			LOG_STREAM(LOG_INFO) << __VA_ARGS__; } while(0)
#define DBG_DEBUG(...)			do { if (Logger::getInstance().isEnabledFor(LOG_DEBUG))			LOG_STREAM(LOG_DEBUG) << __VA_ARGS__; } while(0)
#define DBG_TRACE(...)			do { if (Logger::getInstance().isEnabledFor(LOG_TRACE))			LOG_STREAM(LOG_TRACE) << __VA_ARGS__; } while(0)

/* Inline variants print without header/newline.
   For example, you can use LOG_INFO_INLINE to output progress updates on the same line. */
#define DBG_FATAL_INLINE(...)	do { if (Logger::getInstance().isEnabledFor(LOG_FATAL))			LOG_INLINE_STREAM(LOG_FATAL) << __VA_ARGS__; } while(0)
#define DBG_ERROR_INLINE(...)	do { if (Logger::getInstance().isEnabledFor(LOG_ERROR))			LOG_INLINE_STREAM(LOG_ERROR) << __VA_ARGS__; } while(0)
#define DBG_WARN_INLINE(...)	do { if (Logger::getInstance().isEnabledFor(LOG_WARNING))		LOG_INLINE_STREAM(LOG_WARNING) << __VA_ARGS__; } while(0)
#define DBG_INFO_INLINE(...)	do { if (Logger::getInstance().isEnabledFor(LOG_INFO))			LOG_INLINE_STREAM(LOG_INFO) << __VA_ARGS__; } while(0)
#define DBG_DEBUG_INLINE(...)	do { if (Logger::getInstance().isEnabledFor(LOG_DEBUG))			LOG_INLINE_STREAM(LOG_DEBUG) << __VA_ARGS__; } while(0)
#define DBG_TRACE_INLINE(...)	do { if (Logger::getInstance().isEnabledFor(LOG_TRACE))			LOG_INLINE_STREAM(LOG_TRACE) << __VA_ARGS__; } while(0)

/*
 * Assert with error message.
 * This is a macro that asserts a condition and logs an error message if the condition is false.
 */
#define ASSERT_WITH_ERROR(condition, message) \
		do { \
			if (!(condition)) { \
				DBG_ERROR(__FUNCTION__ << ", line " << __LINE__ << ": " << message); \
				assert(false); \
			} \
		} while(0)
