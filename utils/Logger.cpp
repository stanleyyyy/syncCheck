#include "Logger.h"
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <sys/time.h>

Logger &Logger::getInstance()
{
	static Logger instance;
	return instance;
}

Logger::Logger()
	: m_logLevel(LOG_INFO)
{
}

void Logger::setLogLevel(LogLevel level)
{
	m_logLevel = level;
}

LogLevel Logger::getLogLevel() const
{
	return m_logLevel;
}

void Logger::fatal(const char *format, ...)
{
	if (m_logLevel >= LOG_FATAL) {
		va_list args;
		va_start(args, format);
		log(LOG_FATAL, format, args);
		va_end(args);
	}
}

void Logger::error(const char *format, ...)
{
	if (m_logLevel >= LOG_ERROR) {
		va_list args;
		va_start(args, format);
		log(LOG_ERROR, format, args);
		va_end(args);
	}
}

void Logger::warning(const char *format, ...)
{
	if (m_logLevel >= LOG_WARNING) {
		va_list args;
		va_start(args, format);
		log(LOG_WARNING, format, args);
		va_end(args);
	}
}

void Logger::info(const char *format, ...)
{
	if (m_logLevel >= LOG_INFO) {
		va_list args;
		va_start(args, format);
		log(LOG_INFO, format, args);
		va_end(args);
	}
}

void Logger::debug(const char *format, ...)
{
	if (m_logLevel >= LOG_DEBUG) {
		va_list args;
		va_start(args, format);
		log(LOG_DEBUG, format, args);
		va_end(args);
	}
}

void Logger::trace(const char *format, ...)
{
	if (m_logLevel >= LOG_TRACE) {
		va_list args;
		va_start(args, format);
		log(LOG_TRACE, format, args);
		va_end(args);
	}
}

void Logger::log(LogLevel level, const char *format, va_list args)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);

	char time_str[32];
	strftime(time_str, sizeof(time_str), "%Y%m%d %H:%M:%S", localtime(&tv.tv_sec));

	const char *level_str;
	switch (level) {
	case LOG_FATAL:
		level_str = "FATAL";
		break;
	case LOG_ERROR:
		level_str = "ERROR";
		break;
	case LOG_WARNING:
		level_str = "WARN ";
		break;
	case LOG_INFO:
		level_str = "INFO ";
		break;
	case LOG_DEBUG:
		level_str = "DEBUG";
		break;
	case LOG_TRACE:
		level_str = "TRACE";
		break;
	default:
		level_str = "?????";
		break;
	}

	char msg[MAX_MSG_LENGTH];
	vsnprintf(msg, sizeof(msg), format, args);

	fprintf(stderr, "%s.%03ld [%s] %s\n", time_str, tv.tv_usec / 1000, level_str, msg);
}