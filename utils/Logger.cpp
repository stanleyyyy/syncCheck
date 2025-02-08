#include "Logger.h"
#include <cstdio>
#include <ctime>
#include <sys/time.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <unistd.h>
#include <sys/syscall.h>

#include "Logger.h"
#include <iostream>

Logger& Logger::getInstance()
{
	static Logger instance;
	return instance;
}

Logger::Logger()
	: m_logLevel(LOG_INFO)  // Default to INFO level
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

bool Logger::isEnabledFor(LogLevel level) const
{
	return level <= m_logLevel;
}

void Logger::logMessage(LogLevel level, const std::string& message, bool inlineMode)
{
	if (!isEnabledFor(level)) {
		return;
	}

	// Get current time
	auto now = std::chrono::system_clock::now();
	auto time = std::chrono::system_clock::to_time_t(now);
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

	// Format timestamp and process/thread ID
	std::ostringstream timeStr;
	timeStr << std::put_time(std::localtime(&time), "%Y%m%d %H:%M:%S")
		<< "." << std::setfill('0') << std::setw(3) << ms.count()
		<< " [" << getpid() << "." << syscall(SYS_gettid) << "] ";

	// Truncate message if it exceeds maximum length
	std::string truncatedMessage = message;
	if (truncatedMessage.length() > MAX_MSG_LENGTH) {
		truncatedMessage = truncatedMessage.substr(0, MAX_MSG_LENGTH - 3) + "...";
	}

	// Get level prefix
	const char* levelStr;
	switch (level) {
		case LOG_FATAL:
			levelStr = "FATAL";
			break;
		case LOG_ERROR:
			levelStr = "ERROR";
			break;
		case LOG_WARNING:
			levelStr = "WARN";
			break;
		case LOG_INFO:
			levelStr = "INFO";
			break;
		case LOG_DEBUG:
			levelStr = "DEBUG";
			break;
		case LOG_TRACE:
			levelStr = "TRACE";
			break;
		default:
			levelStr = "UNKNOWN";
			break;
	}

	// Output format: timestamp [pid.tid] [LEVEL] message
	if (inlineMode) {
		std::cout << truncatedMessage << std::flush;
	} else {
		std::cout << timeStr.str() << levelStr << " " << truncatedMessage << std::endl;
	}
}