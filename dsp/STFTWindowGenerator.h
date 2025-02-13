#pragma once

#include <vector>
#include <cmath>
#include <iostream>

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
	 *
	 * If the parameters are invalid (overlap >= windowLength), the generator is marked as invalid.
	 */
	STFTWindowGenerator(int windowLength, int overlap)
	{
		if (overlap >= windowLength) {
			m_valid = false;
			return;
		}
		m_windowLength = windowLength;
		m_overlap = overlap;
		m_hopSize = m_windowLength - m_overlap;
		generateWindows();
	}

	/**
	 * @brief Returns true if the window generator parameters are valid.
	 *
	 * @return True if valid, false otherwise.
	 */
	bool isValid() const { return m_valid; }

	/**
	 * @brief Returns the computed analysis window.
	 *
	 * @return The computed analysis window.
	 */
	const std::vector<double> &getAnalysisWindow() const { return m_analysisWindow; }

	/**
	 * @brief Returns the computed synthesis window.
	 *
	 * @return The computed synthesis window.
	 */
	const std::vector<double> &getSynthesisWindow() const { return m_synthesisWindow; }

	/**
	 * @brief Returns the window length.
	 *
	 * @return The window length.
	 */
	int getWindowLength() const { return m_windowLength; }

	/**
	 * @brief Returns the overlap.
	 *
	 * @return The overlap.
	 */
	int getOverlap() const { return m_overlap; }

	/**
	 * @brief Returns the hop size.
	 *
	 * @return The hop size.
	 */
	int getHopSize() const { return m_hopSize; }

private:
	/**
	 * @brief Generates the analysis and synthesis windows.
	 */
	void generateWindows()
	{
		// Generate analysis window using the Hann window formula.
		m_analysisWindow.resize(m_windowLength);
		for (int m_n = 0; m_n < m_windowLength; m_n++) {
			m_analysisWindow[m_n] = 0.5 * (1.0 - std::cos(2.0 * M_PI * m_n / (m_windowLength - 1)));
		}

		// Compute the denominator for the synthesis window.
		std::vector<double> m_denoms(m_hopSize, 0.0);
		for (int r = 0; r < m_hopSize; r++) {
			for (int j = 0; r + j * m_hopSize < m_windowLength; j++) {
				int m_index = r + j * m_hopSize;
				m_denoms[r] += m_analysisWindow[m_index] * m_analysisWindow[m_index];
			}
		}

		// Compute the synthesis window.
		m_synthesisWindow.resize(m_windowLength);
		for (int m_i = 0; m_i < m_windowLength; m_i++) {
			int r = m_i % m_hopSize;
			if (m_denoms[r] > 1e-12) {
				m_synthesisWindow[m_i] = m_analysisWindow[m_i] / m_denoms[r];
			}
			else {
				m_synthesisWindow[m_i] = 0.0;
			}
		}
	}

	int m_windowLength;
	int m_overlap;
	int m_hopSize;
	bool m_valid = true;
	std::vector<double> m_analysisWindow;
	std::vector<double> m_synthesisWindow;
};
