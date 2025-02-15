#pragma once

#include <vector>
#include <algorithm>
#include <cmath>

/**
 * @brief Structure for tracking delay measurements and statistics.
 */
struct DelayStats {
	static const int HISTORY_SIZE = 5;	///< Number of measurements to keep.
	std::vector<float> m_history;		///< History of delay measurements.
	std::vector<float> m_pslrHistory;	///< History of PSLR measurements.
	float m_filteredDelay;				///< Filtered (median) delay.
	float m_filteredPSLR;				///< Filtered (median) PSLR.
	float m_confidence;					///< Confidence metric based on variance.

	DelayStats()
		: m_filteredDelay(0.0f)
		, m_filteredPSLR(0.0f)
		, m_confidence(0.0f)
	{
		m_history.reserve(HISTORY_SIZE);
		m_pslrHistory.reserve(HISTORY_SIZE);
	}

	/**
	 * @brief Update delay statistics with a new measurement.
	 *
	 * @param new_delay New delay measurement in seconds.
	 * @param new_pslr New PSLR measurement in dB.
	 */
	void update(float new_delay, float new_pslr)
	{
		// // ignore any values with low pslr (below 2.0 dB)
		// if (new_pslr < 2.0f) {
		// 	return;
		// }

		if (m_history.size() >= HISTORY_SIZE) {
			m_history.erase(m_history.begin());
			m_pslrHistory.erase(m_pslrHistory.begin());
		}
		m_history.push_back(new_delay);
		m_pslrHistory.push_back(new_pslr);

		// by default use median
		std::vector<float> sorted = m_history;
		std::sort(sorted.begin(), sorted.end());
		m_filteredDelay = sorted[sorted.size() / 2];

		std::vector<float> sortedPSLR = m_pslrHistory;
		std::sort(sortedPSLR.begin(), sortedPSLR.end());
		m_filteredPSLR = sortedPSLR[sortedPSLR.size() / 2];

		// Use Median Absolute Deviation (MAD) to handle outliers
		if (m_history.size() == HISTORY_SIZE) {
			// Calculate median
			std::vector<float> sorted = m_history;
			std::sort(sorted.begin(), sorted.end());
			float median = sorted[sorted.size() / 2];

			// Calculate absolute deviations from median
			std::vector<float> deviations;
			deviations.reserve(HISTORY_SIZE);
			for (float val : m_history) {
				deviations.push_back(fabsf(val - median));
			}

			// Calculate MAD (median of absolute deviations)
			std::sort(deviations.begin(), deviations.end());
			float mad = deviations[deviations.size() / 2];

			// For a normal distribution, MAD_SCALE = 1.4826 converts MAD to standard deviation
			// OUTLIER_THRESHOLD = 3.0 is the common "3-sigma rule" - data within 3 std devs
			const float MAD_SCALE = 1.4826f;	  // 1/Φ^(-1)(3/4) where Φ is normal CDF
			const float OUTLIER_THRESHOLD = 3.0f; // Points >3σ from median considered outliers
			float scaled_mad = mad * MAD_SCALE;	  // Convert MAD to equivalent std deviation
			std::vector<float> inliers;

			for (float val : m_history) {
				if (fabsf(val - median) <= OUTLIER_THRESHOLD * scaled_mad) {
					inliers.push_back(val);
				}
			}

			// Use median of inliers as filtered delay
			if (!inliers.empty()) {
				std::sort(inliers.begin(), inliers.end());
				m_filteredDelay = inliers[inliers.size() / 2];
			}
		}

		// Calculate confidence based on variance.
		float sum = 0.0f;
		for (float d : m_history) {
			float diff = d - m_filteredDelay;
			sum += diff * diff;
		}
		float variance = sum / m_history.size();
		m_confidence = 1.0f / (1.0f + sqrtf(variance));
	}
};
