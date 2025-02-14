#pragma once

#include <vector>
#include <cmath>
#include <iostream>
#include "Logger.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class STFTWindowGenerator
{
public:
	/**
	 * @brief Constructor initializes window parameters and generates windows.
	 *
	 * @param windowLength Length of the analysis window.
	 * @param overlap Overlap between consecutive analysis windows.
	 */
	STFTWindowGenerator(int windowLength, int overlap)
	{
		ASSERT_WITH_ERROR(overlap < windowLength, "Overlap must be less than window length.");
		ASSERT_WITH_ERROR(overlap == windowLength/2 || overlap == (3*windowLength)/4, "Overlap must be 50% or 75% of window length.");

		m_windowLength = windowLength;
		m_overlap = overlap;
		m_hopSize = m_windowLength - m_overlap;

		generateWindows();
	}

	/**
	 * @brief Returns the computed analysis window.
	 */
	const std::vector<double> &getAnalysisWindow() const { return m_analysisWindow; }

	/**
	 * @brief Returns the computed synthesis window.
	 */
	const std::vector<double> &getSynthesisWindow() const { return m_synthesisWindow; }

	/**
	 * @brief Returns the window length.
	 */
	int getWindowLength() const { return m_windowLength; }

	/**
	 * @brief Returns the overlap.
	 */
	int getOverlap() const { return m_overlap; }

	/**
	 * @brief Returns the hop size.
	 */
	int getHopSize() const { return m_hopSize; }

private:
	/**
	 * @brief Generates the analysis and synthesis windows.
	 */
	void generateWindows()
	{
		// Standard Hann (analysis) window
		m_analysisWindow.resize(m_windowLength);
		for (int n = 0; n < m_windowLength; n++) {
			m_analysisWindow[n] = 0.5 * (1.0 - std::cos(2.0 * M_PI * n / (m_windowLength - 1)));
		}

		// Sum-of-squares denominators for each residue class mod hopSize
		std::vector<double> denoms(m_hopSize, 0.0);
		for (int n = 0; n < m_windowLength; n++) {
			int r = n % m_hopSize;
			denoms[r] += m_analysisWindow[n] * m_analysisWindow[n];
		}

		// Synthesis window: analysisWindow[n] / denoms[r]
		m_synthesisWindow.resize(m_windowLength);
		for (int n = 0; n < m_windowLength; n++) {
			int r = n % m_hopSize;
			if (denoms[r] > 1e-12) {
				m_synthesisWindow[n] = m_analysisWindow[n] / denoms[r];
			} else {
				m_synthesisWindow[n] = 0.0;
			}
		}
	}

	int m_windowLength;
	int m_overlap;
	int m_hopSize;
	std::vector<double> m_analysisWindow;
	std::vector<double> m_synthesisWindow;
};
