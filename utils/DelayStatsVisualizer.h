#pragma once

#include "Logger.h"
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

public:
	DelayStatsVisualizer(const std::string &outputFile, bool useColors, float maxDelay = 5.0f, unsigned int width = 40)
		: m_useColors(useColors)
		, m_dumpId(0)
		, m_maxDelay(maxDelay)
		, m_width(width)
	{
		if (!outputFile.empty()) {
			m_outputFile.open(outputFile);
		}
	}

	/**
	 * @brief Visualize delay statistics for each channel assignment.
	 *
	 * Outputs channel assignment details along with delay, confidence, and PSLR.
	 *
	 * @param estimates Vector of tuples containing (ChannelAssignment, delay, confidence, PSLR) for each assignment.
	 */
	void visualizeDelays(const std::vector<std::tuple<FFTSyncCheck::ChannelAssignment, float, float, float>> &estimates)
	{
		std::stringstream ss;
		ss << std::fixed << std::setprecision(3);

		// Calculate center position
		int center = m_width / 2;
		ss << "Delay channel[band] -> channel[band] (ms), each division = " << (m_maxDelay / (center - 1)) << " ms\n";

		for (size_t i = 0; i < estimates.size(); ++i) {
			const FFTSyncCheck::ChannelAssignment &assignment = std::get<0>(estimates[i]);
			const double delay = std::get<1>(estimates[i]);
			const double confidence = std::get<2>(estimates[i]);
			const double pslr = std::get<3>(estimates[i]);

			// Skip if the assignment is to the same channel and band.
			if (assignment.refChannel == assignment.sigChannel && assignment.refBand == assignment.sigBand) {
				continue;
			}

			std::string color;
			if (m_useColors) {
				if (pslr > 5.0f)
					color = ANSI_COLOR_GREEN;
				else if (pslr > 2.0)
					color = ANSI_COLOR_YELLOW;
				else
					color = ANSI_COLOR_RED;
			}

			std::string assignStr = ""
								+ std::to_string(assignment.refChannel) + "["
								+ std::to_string(assignment.refBand) + "] -> "
								+ std::to_string(assignment.sigChannel) + "["
								+ std::to_string(assignment.sigBand) + "]";

			// Updated label: output "Assignment X" always.
			ss << (m_useColors ? color : "") << assignStr << ": "
			   << std::setw(8) << delay << " ms [PSLR: "
			   << std::fixed << std::setprecision(3) << std::setw(6) << pslr  // Fixed width of 6 for PSLR
			   << " dB, confidence: " << std::setw(6) << confidence << "]"
			   << (m_useColors ? ANSI_COLOR_RESET : "") << " |";

			// ASCII visualization
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

		// Output to console
		std::cout << ss.str() << std::endl << std::flush;

		// Output to file if opened
		if (m_outputFile.is_open()) {
			m_outputFile << std::setw(6) << std::setfill(' ') << m_dumpId++ << ", ";
			for (size_t i = 0; i < estimates.size(); ++i) {
				const FFTSyncCheck::ChannelAssignment &assignment = std::get<0>(estimates[i]);
				const double delay = std::get<1>(estimates[i]);
				const double confidence = std::get<2>(estimates[i]);
				const double pslr = std::get<3>(estimates[i]);

				// Skip if the assignment is to the same channel and band.
				if (assignment.refChannel == assignment.sigChannel && assignment.refBand == assignment.sigBand) {
					continue;
				}

				std::string assignStr = ""
								+ std::to_string(assignment.refChannel) + "["
								+ std::to_string(assignment.refBand) + "] -> "
								+ std::to_string(assignment.sigChannel) + "["
								+ std::to_string(assignment.sigBand) + "]";

				m_outputFile << "Assignment " << assignStr << ": "
				             << std::fixed << std::setprecision(3) << std::setw(8) << delay
				             << " ms, confidence: " << std::setw(5) << confidence << ", PSLR: "
				             << std::fixed << std::setprecision(3) << std::setw(5) << pslr;

				if (i < estimates.size() - 1)
					m_outputFile << ", ";
			}
			m_outputFile << std::endl;
		}
	}
};