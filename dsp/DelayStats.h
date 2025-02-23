#pragma once

#include <vector>
#include <algorithm>
#include <cmath>

/**
 * @brief Structure for holding the delay calculation results.
 */
struct DelayCalculationResult {
	float delay_ms;
	float pslr_dB;
	float ref_rms_dB;
	float sig_rms_dB;

	DelayCalculationResult()
		: delay_ms(0.0f)
		, pslr_dB(0.0f)
		, ref_rms_dB(0.0f)
		, sig_rms_dB(0.0f)
	{
	}

	DelayCalculationResult(float delay_ms, float pslr_dB, float ref_rms_dB, float sig_rms_dB)
		: delay_ms(delay_ms)
		, pslr_dB(pslr_dB)
		, ref_rms_dB(ref_rms_dB)
		, sig_rms_dB(sig_rms_dB)
	{
	}
};

/**
 * @brief Structure for tracking delay measurements and statistics.
 */
struct DelayStats {
	/**
	 * @brief Number of measurements to keep.
	 */
	static const int HISTORY_SIZE = 5;

	/**
	 * @brief History of measurements.
	 */
	std::vector<DelayCalculationResult> m_history;

	/**
	 * @brief Filtered (median) delay.
	 */
	float m_filteredDelay;

	/**
	 * @brief Filtered (median) PSLR.
	 */
	float m_filteredPSLR;

	/**
	 * @brief Filtered (median) reference RMS.
	 */
	float m_filteredRefRMS;

	/**
	 * @brief Filtered (median) signal RMS.
	 */
	float m_filteredSigRMS;

	/**
	 * @brief Confidence metric based on variance.
	 */
	float m_confidence;

	DelayStats()
		: m_filteredDelay(0.0f)
		, m_filteredPSLR(0.0f)
		, m_confidence(0.0f)
	{
		m_history.reserve(HISTORY_SIZE);
	}

	/**
	 * @brief Update delay statistics with a new measurement.
	 *
	 * @param result New delay calculation result containing delay, PSLR and RMS measurements.
	 */
	void update(DelayCalculationResult result)
	{
		if (m_history.size() >= HISTORY_SIZE) {
			m_history.erase(m_history.begin());
		}
		m_history.push_back(result);

		// Calculate medians
		m_filteredDelay = calculateMedian(&DelayCalculationResult::delay_ms);
		m_filteredPSLR = calculateMedian(&DelayCalculationResult::pslr_dB);
		m_filteredRefRMS = calculateMedian(&DelayCalculationResult::ref_rms_dB);
		m_filteredSigRMS = calculateMedian(&DelayCalculationResult::sig_rms_dB);

		// Use Median Absolute Deviation (MAD) to handle outliers
		if (m_history.size() == HISTORY_SIZE) {
			float median = m_filteredDelay;

			// Calculate absolute deviations from median
			std::vector<float> deviations;
			deviations.reserve(HISTORY_SIZE);
			for (const auto& res : m_history) {
				deviations.push_back(fabsf(res.delay_ms - median));
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
			inliers.reserve(HISTORY_SIZE);
			for (const auto& res : m_history) {
				if (fabsf(res.delay_ms - median) <= OUTLIER_THRESHOLD * scaled_mad) {
					inliers.push_back(res.delay_ms);
				}
			}

			// Use median of inliers as filtered delay
			if (!inliers.empty()) {
				std::sort(inliers.begin(), inliers.end());
				m_filteredDelay = inliers[inliers.size() / 2];
			}
		}

		// Calculate confidence based on variance
		float sum = 0.0f;
		for (const auto& res : m_history) {
			float diff = res.delay_ms - m_filteredDelay;
			sum += diff * diff;
		}
		float variance = sum / m_history.size();
		m_confidence = 1.0f / (1.0f + sqrtf(variance));
	}

private:

	/**
	 * @brief Calculate median value from history using specified accessor function
	 * @param accessor Function to access the desired property from DelayCalculationResult
	 * @return Median value of the specified property
	 */
	float calculateMedian(float (DelayCalculationResult::* accessor)) const
	{
		if (m_history.empty()) {
			return 0.0f;
		}

		std::vector<float> values;
		values.reserve(m_history.size());
		for (const auto& result : m_history) {
			values.push_back(result.*accessor);
		}
		std::sort(values.begin(), values.end());
		return values[values.size() / 2];
	}
};
