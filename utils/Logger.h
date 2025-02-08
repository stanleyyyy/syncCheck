#pragma once
#include <string>

enum LogLevel { LOG_FATAL = 0, LOG_ERROR, LOG_WARNING, LOG_INFO, LOG_DEBUG, LOG_TRACE };

class Logger
{
public:
	static Logger &getInstance();
	void setLogLevel(LogLevel level);
	LogLevel getLogLevel() const;

	void fatal(const char *format, ...);
	void error(const char *format, ...);
	void warning(const char *format, ...);
	void info(const char *format, ...);
	void debug(const char *format, ...);
	void trace(const char *format, ...);

private:
	Logger();
	void log(LogLevel level, const char *format, va_list args);

	LogLevel m_logLevel;
	static constexpr size_t MAX_MSG_LENGTH = 1024;
};

#define LOG_FATAL(...) Logger::getInstance().fatal(__VA_ARGS__)
#define LOG_ERROR(...) Logger::getInstance().error(__VA_ARGS__)
#define LOG_WARNING(...) Logger::getInstance().warning(__VA_ARGS__)
#define LOG_INFO(...) Logger::getInstance().info(__VA_ARGS__)
#define LOG_DEBUG(...) Logger::getInstance().debug(__VA_ARGS__)
#define LOG_TRACE(...) Logger::getInstance().trace(__VA_ARGS__)
