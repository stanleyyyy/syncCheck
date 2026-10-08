#pragma once

#include "Logger.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include "SyncCheckConfig.h"
#include "SyncCheck.h"

// ANSI color codes
#define ANSI_COLOR_RED "\x1b[31m"
#define ANSI_COLOR_GREEN "\x1b[32m"
#define ANSI_COLOR_YELLOW "\x1b[33m"
#define ANSI_COLOR_BLUE "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN "\x1b[36m"
#define ANSI_COLOR_RESET "\x1b[0m"

class DelayStatsVisualizer
{
private:
	std::ofstream m_outputFile;
	bool m_useColors;
	unsigned int m_dumpId;
	float m_maxDelay;		// Maximum delay range (+/-)
	unsigned int m_width;	// Width of ASCII visualization
	bool m_csvFormat;		// Enable CSV dumping if true

private:
	std::string formatTime(float timestamp)
	{
		int hours = static_cast<int>(timestamp) / 3600;
		int minutes = (static_cast<int>(timestamp) % 3600) / 60;
		int seconds = static_cast<int>(timestamp) % 60;
		int milliseconds = static_cast<int>((timestamp - static_cast<int>(timestamp)) * 1000);

		std::stringstream ss;
		ss << std::setfill('0') << std::setw(2) << hours << ":"
		   << std::setfill('0') << std::setw(2) << minutes << ":"
		   << std::setfill('0') << std::setw(2) << seconds << "."
		   << std::setfill('0') << std::setw(3) << milliseconds;
		return ss.str();
	}

public:
	/**
	 * @brief Constructor for DelayStatsVisualizer.
	 *
	 * @param outputFile Path to the output file. If the filename ends with ".csv", CSV dumping is used.
	 * @param useColors Whether to use ANSI colors for console output.
	 * @param maxDelay Maximum delay range (+/-) (default is 5.0 ms).
	 * @param width Width of ASCII visualization (default is 40).
	 */
	DelayStatsVisualizer(const std::string &outputFile, bool useColors, float maxDelay = 5.0f, unsigned int width = 40)
		: m_useColors(useColors)
		, m_dumpId(0)
		, m_maxDelay(maxDelay)
		, m_width(width)
		, m_csvFormat(false)
	{
		if (!outputFile.empty()) {
			// Check if the file extension is ".csv" to enable CSV dumping.
			if (outputFile.size() >= 4 && outputFile.compare(outputFile.size() - 4, 4, ".csv") == 0) {
				m_csvFormat = true;
			}
			m_outputFile.open(outputFile);
		}
	}

	/**
	 * @brief Visualize delay statistics for each channel assignment.
	 *
	 * Outputs channel assignment details along with delay, confidence, PSLR, and the provided timestamp.
	 *
	 * @param estimates Vector of tuples containing (ChannelAssignment, delay, confidence, PSLR) for each assignment.
	 * @param timestamp Accumulated time (in seconds) from syncAnalyzer, used as timestamp.
	 */
	void visualizeDelays(const std::vector<FFTSyncCheck::DelayStatsReport> &estimates, float timestamp)
	{
		std::stringstream ss;
		ss << std::fixed << std::setprecision(3);

		// Print dump header with timestamp information.
		ss << "Timestamp (s): " << timestamp << "\n";
		ss << "Delay channel[band] -> channel[band] (ms), each division = " << (m_maxDelay / (m_width/2 - 1)) << " ms\n";

		// Process each assignment.
		for (size_t i = 0; i < estimates.size(); ++i) {
			const FFTSyncCheck::ChannelAssignment &assignment = estimates[i].assignment;
			const double delay = estimates[i].filteredDelay;
			const double confidence = estimates[i].confidence;
			const double pslr = estimates[i].filteredPSLR;
			const double refRMS = estimates[i].filteredRefRMS;
			const double sigRMS = estimates[i].filteredSigRMS;

			// Skip if the assignment is to the same channel and band.
			if (assignment.refChannel == assignment.sigChannel && assignment.refBand == assignment.sigBand) {
				continue;
			}

			std::string color;
			if (m_useColors) {
				if (pslr > 5.0f)
					color = ANSI_COLOR_GREEN;
				else if (pslr > 2.0f)
					color = ANSI_COLOR_YELLOW;
				else
					color = ANSI_COLOR_RED;
			}

			std::string assignStr = std::to_string(assignment.refChannel) + "[" + std::to_string(assignment.refBand)
									+ "] -> " + std::to_string(assignment.sigChannel) + "[" + std::to_string(assignment.sigBand) + "]";

			ss << (m_useColors ? color : "")
			   << "Time: " << formatTime(timestamp) << ", "
			   << assignStr << ": "
			   << std::setw(8) << delay << " ms [PSLR: "
			   << std::setw(6) << pslr << " dB, confidence: " << std::setw(6) << confidence << ", refRMS: " << std::setw(6) << refRMS << " dB, sigRMS: " << std::setw(6) << sigRMS << " dB]"
			   << (m_useColors ? ANSI_COLOR_RESET : "") << " |";

			int center = m_width / 2;
			int position = center + static_cast<int>((delay / m_maxDelay) * (center - 1));
			position = std::max(0, std::min(static_cast<int>(m_width - 1), position));

			for (unsigned int j = 0; j < m_width; ++j) {
				if ((int)j == position) {
					ss << (m_useColors ? color : "") << "*" << (m_useColors ? ANSI_COLOR_RESET : "");
				} else if ((int)j == center) {
					ss << "|";  // Center mark
				} else {
					ss << "-";
				}
			}
			ss << "|\n";
		}

		std::cout << ss.str() << std::endl << std::flush;

		// Output to file if opened.
		if (m_outputFile.is_open()) {
			if (m_csvFormat) {
				// CSV dumping: one row per valid assignment.
				// CSV format: timestamp,dumpId,refChannel,refBand,sigChannel,sigBand,delay,confidence,pslr
				for (size_t i = 0; i < estimates.size(); ++i) {
					const FFTSyncCheck::ChannelAssignment &assignment = estimates[i].assignment;
					const double delay = estimates[i].filteredDelay;
					const double confidence = estimates[i].confidence;
					const double pslr = estimates[i].filteredPSLR;
					const double refRMS = estimates[i].filteredRefRMS;
					const double sigRMS = estimates[i].filteredSigRMS;

					if (assignment.refChannel == assignment.sigChannel && assignment.refBand == assignment.sigBand) {
						continue;
					}

					m_outputFile << timestamp << ","
					             << m_dumpId << ","
					             << assignment.refChannel << ","
					             << assignment.refBand << ","
					             << assignment.sigChannel << ","
					             << assignment.sigBand << ","
					             << std::fixed << std::setprecision(3) << delay << ","
					             << std::fixed << std::setprecision(3) << confidence << ","
					             << std::fixed << std::setprecision(3) << pslr << ","
					             << std::fixed << std::setprecision(3) << refRMS << ","
					             << std::fixed << std::setprecision(3) << sigRMS << std::endl;
				}
				m_dumpId++;
			} else {
				// ASCII dumping to file including the timestamp.
				m_outputFile << "Time: " << formatTime(timestamp) << ", ";
				m_outputFile << std::setw(6) << m_dumpId++ << ", ";
				for (size_t i = 0; i < estimates.size(); ++i) {
					const FFTSyncCheck::ChannelAssignment &assignment = estimates[i].assignment;
					const double delay = estimates[i].filteredDelay;
					const double confidence = estimates[i].confidence;
					const double pslr = estimates[i].filteredPSLR;
					const double refRMS = estimates[i].filteredRefRMS;
					const double sigRMS = estimates[i].filteredSigRMS;

					if (assignment.refChannel == assignment.sigChannel && assignment.refBand == assignment.sigBand) {
						continue;
					}

					std::string assignStr = std::to_string(assignment.refChannel) + "[" + std::to_string(assignment.refBand)
											+ "] -> " + std::to_string(assignment.sigChannel) + "[" + std::to_string(assignment.sigBand) + "]";
					m_outputFile << "Assignment " << assignStr << ": "
					             << std::fixed << std::setprecision(3) << std::setw(8) << delay
					             << " ms, confidence: " << std::setw(5) << confidence << ", PSLR: "
					             << std::fixed << std::setprecision(3) << std::setw(5) << pslr << ", refRMS: " << std::setw(5) << refRMS << " dB, sigRMS: " << std::setw(5) << sigRMS << " dB";
					if (i < estimates.size() - 1)
						m_outputFile << ", ";
				}
				m_outputFile << std::endl;
			}
		}
	}
};