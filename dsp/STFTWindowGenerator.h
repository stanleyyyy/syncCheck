#pragma once

#include <vector>
#include <cmath>
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

enum class WindowType
{
	HANN,
	SINE,
	BLACKMAN_HARRIS
};

class STFTWindowGenerator
{
public:
	/**
	* @brief Constructor initializes window parameters and generates windows.
	*
	* @param windowLength Length of the analysis window.
	* @param overlap Overlap between consecutive analysis windows.
	* @param type The type of window to use (HANN or SINE).
	*/
	STFTWindowGenerator(int windowLength, int overlap, WindowType type = WindowType::BLACKMAN_HARRIS);
	~STFTWindowGenerator();
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
	 * @brief Generates the analysis and synthesis windows based on the selected window type.
	 */
	void generateWindows()
	{
		m_analysisWindow.resize(m_windowLength);

		switch (m_type) {
		case WindowType::SINE:
			// Generate sine window.
			for (int n = 0; n < m_windowLength; n++) {
				m_analysisWindow[n] = std::sin(M_PI * (n + 0.5) / m_windowLength);
			}
			// For sine window, analysis and synthesis windows are identical.
			m_synthesisWindow = m_analysisWindow;
			break;

		case WindowType::BLACKMAN_HARRIS: {
			// 4-term Blackman-Harris window coefficients
			const double a0 = 0.35875;
			const double a1 = 0.48829;
			const double a2 = 0.14128;
			const double a3 = 0.01168;

			for (int n = 0; n < m_windowLength; n++) {
				double x = 2.0 * M_PI * n / (m_windowLength - 1);
				m_analysisWindow[n] = a0 - a1 * std::cos(x) + a2 * std::cos(2 * x) - a3 * std::cos(3 * x);
			}
			break;
		}

		default: // WindowType::HANN
			// Generate standard Hann analysis window.
			for (int n = 0; n < m_windowLength; n++) {
				m_analysisWindow[n] = 0.5 * (1.0 - std::cos(2.0 * M_PI * n / (m_windowLength - 1)));
			}
			break;
		}

		if (m_type != WindowType::SINE) {
			// Compute sum-of-squares denominators for each residue class mod hopSize.
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
				}
				else {
					m_synthesisWindow[n] = 0.0;
				}
			}
		}

		// Compensate gain loss of the synthesis window.
		double windowEnergyCompensation = 1.0;
		if ((m_type == WindowType::BLACKMAN_HARRIS) && (m_overlap != m_windowLength / 2)) {
			// Blackman-Harris has about 37.5% of the energy of a rectangular window
			windowEnergyCompensation = 2.67; // ~1/0.375
		}

		const float boost = (static_cast<float>(m_windowLength) / static_cast<float>(m_hopSize)) * windowEnergyCompensation;
		for (int n = 0; n < m_windowLength; n++) {
			m_synthesisWindow[n] *= boost;
		}
	}

	int m_windowLength;
	int m_overlap;
	int m_hopSize;
	WindowType m_type;
	std::vector<double> m_analysisWindow;
	std::vector<double> m_synthesisWindow;
};