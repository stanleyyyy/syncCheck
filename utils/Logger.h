#pragma once
#include <string>
#include <sstream>

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
	friend class LogBuffer;
private:
	Logger();
	Logger(const Logger&) = delete;
	Logger& operator=(const Logger&) = delete;
	void logMessage(LogLevel level, const std::string& message);
	LogLevel m_logLevel;
	static constexpr size_t MAX_MSG_LENGTH = 1024;
};

class LogBuffer {
public:
	LogBuffer(Logger& logger, LogLevel level)
		: m_logger(logger)
		, m_level(level)
	{}
	~LogBuffer() {
		if (m_logger.isEnabledFor(m_level)) {
			m_logger.logMessage(m_level, m_stream.str());
		}
	}
	template<typename T>
	LogBuffer& operator<<(const T& value) {
		m_stream << value;
		return *this;
	}
	LogBuffer& operator<<(const char* value) {
		m_stream << value;
		return *this;
	}
private:
	Logger& m_logger;
	LogLevel m_level;
	std::ostringstream m_stream;
};

#define LOG_STREAM(level) LogBuffer(Logger::getInstance(), level)

#define DBG_FATAL(...) do { if (Logger::getInstance().isEnabledFor(LOG_FATAL)) LOG_STREAM(LOG_FATAL) << __VA_ARGS__; } while(0)
#define DBG_ERROR(...) do { if (Logger::getInstance().isEnabledFor(LOG_ERROR)) LOG_STREAM(LOG_ERROR) << __VA_ARGS__; } while(0)
#define DBG_WARN(...)  do { if (Logger::getInstance().isEnabledFor(LOG_WARNING)) LOG_STREAM(LOG_WARNING) << __VA_ARGS__; } while(0)
#define DBG_INFO(...)  do { if (Logger::getInstance().isEnabledFor(LOG_INFO)) LOG_STREAM(LOG_INFO) << __VA_ARGS__; } while(0)
#define DBG_DEBUG(...) do { if (Logger::getInstance().isEnabledFor(LOG_DEBUG)) LOG_STREAM(LOG_DEBUG) << __VA_ARGS__; } while(0)
#define DBG_TRACE(...) do { if (Logger::getInstance().isEnabledFor(LOG_TRACE)) LOG_STREAM(LOG_TRACE) << __VA_ARGS__; } while(0)
