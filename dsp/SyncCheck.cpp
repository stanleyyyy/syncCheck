#include "SyncCheck.h"
#include "Logger.h"
#include <iomanip>

FFTSyncCheck::FFTSyncCheck(const SyncCheckConfig& config)
	: m_config(config)
	, m_bands(config.getNumBands())
	, m_delayStats(config.getNumBands())
	, m_analysisForwardCfg(kiss_fftr_alloc(config.getBlockSize(), 0, nullptr, nullptr), free)
	, m_analysisInverseCfg(kiss_fftr_alloc(config.getBlockSize(), 1, nullptr, nullptr), free)
	, m_analysisTimeData(config.getBlockSize())
	, m_analysisFreqData(config.getBlockSize() / 2 + 1)
	, m_correlationData(config.getBlockSize() / 2 + 1)
	, m_paddedForwardCfg(kiss_fftr_alloc(config.getBlockSize() * config.getInterpFactor(), 0, nullptr, nullptr), free)
	, m_paddedInverseCfg(kiss_fftr_alloc(config.getBlockSize() * config.getInterpFactor(), 1, nullptr, nullptr), free)
	, m_paddedTimeData(config.getBlockSize() * config.getInterpFactor())
	, m_paddedCorrData(config.getBlockSize() * config.getInterpFactor() / 2 + 1)
	, m_paddedFreqData(config.getBlockSize() * config.getInterpFactor() / 2 + 1)
{
	// Use bands from config
	m_bands = config.getBands();
}

FFTSyncCheck::~FFTSyncCheck() = default;

void FFTSyncCheck::extractBand(const std::vector<kiss_fft_cpx>& input, std::vector<kiss_fft_cpx>& output, std::size_t band_idx)
{
	std::fill(output.begin(), output.end(), kiss_fft_cpx{0.0f, 0.0f});
	if (band_idx < m_config.getNumBands()) {
		int start = m_bands[band_idx].getStartBin(m_config.getBlockSize(), m_config.getSampleRate());
		int end = m_bands[band_idx].getEndBin(m_config.getBlockSize(), m_config.getSampleRate());
		for (int i = start; i <= end; i++) {
			output[i] = input[i];
		}
	}
}

float FFTSyncCheck::calculateBandDelayFFT(const std::vector<kiss_fft_cpx>& fft_data, std::size_t band1, std::size_t band2)
{
	const std::size_t padded_size = m_config.getBlockSize() * m_config.getInterpFactor();
	const std::size_t padded_freq_size = padded_size / 2 + 1;
	const std::size_t orig_freq_size = m_config.getBlockSize() / 2 + 1;

	// Zero out the padded frequency buffer.
	std::fill(m_paddedFreqData.begin(), m_paddedFreqData.end(), kiss_fft_cpx{0.0f, 0.0f});

	// Copy the original FFT data into the padded buffer with scaling.
	for (std::size_t i = 0; i < orig_freq_size; i++) {
		m_paddedFreqData[i].r = fft_data[i].r * m_config.getInterpFactor();
		m_paddedFreqData[i].i = fft_data[i].i * m_config.getInterpFactor();
	}

	// Determine band shift and width
	const double band_shift_Hz = m_bands[band2].m_startFreq - m_bands[band1].m_startFreq;
	const double band_width_Hz = m_bands[band1].m_endFreq - m_bands[band1].m_startFreq + 1;

	// Convert to bins
	const std::size_t band_shift = band_shift_Hz * m_config.getBlockSize() / m_config.getSampleRate();
	const std::size_t band_width = band_width_Hz * m_config.getBlockSize() / m_config.getSampleRate();

	// Zero out the padded correlation buffer.
	std::fill(m_paddedCorrData.begin(), m_paddedCorrData.end(), kiss_fft_cpx{0.0f, 0.0f});

	// Compute the normalized cross-correlation product over the band.
	for (std::size_t i = 0; i < band_width; i++) {
		std::size_t bin1 = m_bands[band1].getStartBin(m_config.getBlockSize(), m_config.getSampleRate()) + i;
		std::size_t bin2 = bin1 + band_shift;

		if (bin2 <= m_bands[band2].getEndBin(m_config.getBlockSize(), m_config.getSampleRate()) &&
			bin1 < orig_freq_size && bin2 < orig_freq_size) {

			// Use the padded frequency data instead of the original fft_data.
			float real1 = m_paddedFreqData[bin1].r;
			float imag1 = m_paddedFreqData[bin1].i;
			float real2 = m_paddedFreqData[bin2].r;
			float imag2 = m_paddedFreqData[bin2].i;

			// Normalize the magnitudes.
			float mag1 = sqrtf(real1 * real1 + imag1 * imag1);
			float mag2 = sqrtf(real2 * real2 + imag2 * imag2);

			if (mag1 > 1e-10f && mag2 > 1e-10f) {
				// Use single scaling factor to maintain phase relationships
				float scale = 1.0f / (mag1 * mag2);

				// Complex multiplication (conjugate of second signal)
				m_paddedCorrData[bin1].r = (real2 * real1 + imag2 * imag1) * scale;
				m_paddedCorrData[bin1].i = (imag2 * real1 - real2 * imag1) * scale;
			}
		}
	}

	// Perform inverse FFT on the zero-padded cross-correlation product.
	kiss_fftri(m_paddedInverseCfg.get(), m_paddedCorrData.data(), m_paddedTimeData.data());

	// Find the index of the maximum correlation peak.
	float max_corr = 0.0f;
	int max_idx = 0;

	// First find absolute maximum
	for (std::size_t i = 0; i < padded_size; i++) {
		float abs_val = fabsf(m_paddedTimeData[i]);
		if (abs_val > max_corr) {
			max_corr = abs_val;
			max_idx = i;
		}
	}

	// Check if we have a symmetric positive/negative peak pattern
	const float PEAK_THRESHOLD = 0.5f;  // Threshold for secondary peak
	const std::size_t MAX_PEAK_SEPARATION = static_cast<std::size_t>(0.5f * m_config.getSampleRate() * m_config.getInterpFactor() / 1000.0f); // 0.5ms max separation

	float main_val = m_paddedTimeData[max_idx];
	int opposite_peak_idx = -1;
	float opposite_peak_val = 0.0f;

	// Search for opposite peak within a small window
	for (std::size_t i = std::max(0, max_idx - (int)MAX_PEAK_SEPARATION);
		 i < std::min(padded_size, max_idx + MAX_PEAK_SEPARATION); i++) {
		float val = m_paddedTimeData[i];
		if (main_val * val < 0 && fabsf(val) > max_corr * PEAK_THRESHOLD) {  // Opposite sign and significant
			if (fabsf(val) > fabsf(opposite_peak_val)) {
				opposite_peak_val = val;
				opposite_peak_idx = i;
			}
		}
	}

	// If we found a symmetric positive/negative peak pattern
	if (opposite_peak_idx >= 0) {
		// Use weighted average, favoring the positive peak
		if (main_val > 0) {
			max_idx = static_cast<int>(roundf(0.6f * max_idx + 0.4f * opposite_peak_idx));
		} else {
			max_idx = static_cast<int>(roundf(0.4f * max_idx + 0.6f * opposite_peak_idx));
		}
	}

	// // Save correlation data for visualization
	// {
	// 	static int dump_count = 0;
	// 	char filename[256];
	// 	snprintf(filename, sizeof(filename), "correlation_data_%zu_%zu_%d.bin", band1, band2, dump_count);
	// 	FILE* f = fopen(filename, "wb");
	// 	if (f) {
	// 		// Save parameters
	// 		int32_t params[] = {(int)padded_size, (int)m_config.getSampleRate(), (int)max_idx};
	// 		fwrite(params, sizeof(int32_t), 3, f);
	// 		// Save correlation data
	// 		fwrite(m_paddedTimeData.data(), sizeof(float), padded_size, f);
	// 		fclose(f);
	// 	}
	// 	// Only increment dump count after processing last band pair
	// 	if (band2 == m_config.getNumBands() - 1) {
	// 		dump_count++;
	// 	}
	// }

	{
		float delay = 0.0f;
		if (max_idx > (int)padded_size / 2) {
			delay = -((int)padded_size - max_idx) / (static_cast<float>(m_config.getSampleRate()) * m_config.getInterpFactor());
		} else {
			delay = max_idx / (static_cast<float>(m_config.getSampleRate()) * m_config.getInterpFactor());
		}
		DBG_INFO("Delay " << band1 << "-" << band2 << ": " << delay * 1000.0f << " ms");
	}

	if (band2 == m_config.getNumBands() - 1) {
		DBG_INFO("");
	}

	// Apply parabolic interpolation for sub-sample accuracy.
	// Only apply if we have identified a single peak
	float alpha = m_paddedTimeData[(max_idx + padded_size - 1) % padded_size];
	float beta = m_paddedTimeData[max_idx];
	float gamma = m_paddedTimeData[(max_idx + 1) % padded_size];

	// p will be in range [-0.5, 0.5], indicating the fractional sample offset
	float p = 0.5f * (alpha - gamma) / (alpha - 2.0f * beta + gamma);
	float interpolated_idx = max_idx + (opposite_peak_idx < 0 ? p : 0);

	// Convert the index to a time delay (accounting for the interpolation factor).
	float delay = 0.0f;
	if (interpolated_idx > padded_size / 2) {
		delay = -(padded_size - interpolated_idx) / (static_cast<float>(m_config.getSampleRate()) * m_config.getInterpFactor());
	} else {
		delay = interpolated_idx / (static_cast<float>(m_config.getSampleRate()) * m_config.getInterpFactor());
	}

	return delay;
}

void FFTSyncCheck::printDelayStats(const std::vector<float>& expected_delays_ms)
{
	DBG_INFO("=== Delay Statistics and Measurements ===");
	DBG_INFO("Band | Expected (ms) | Delay (ms) | Error (ms) | Confidence | Status");
	DBG_INFO("-----+--------------+------------+-----------+-----------+-------------");

	for (std::size_t i = 1; i < m_config.getNumBands(); i++) {
		float delay_ms = m_delayStats[i].m_filteredDelay * 1000.0f;
		float conf = m_delayStats[i].m_confidence;
		const char *status = "OK          ";
		if (conf < 0.5f)
			status = "UNRELIABLE  ";

		float expected = 0.0f;
		float error = delay_ms;
		if (!expected_delays_ms.empty() && i < (std::size_t)expected_delays_ms.size()) {
			expected = expected_delays_ms[i];
			error = delay_ms - expected;
		}

		DBG_INFO(std::setw(4) << i
			<< " | " << std::showpos << std::setw(11) << std::fixed << std::setprecision(3) << expected
			<< " | " << std::showpos << std::setw(9)  << std::fixed << std::setprecision(3) << delay_ms
			<< " | " << std::showpos << std::setw(8)  << std::fixed << std::setprecision(3) << error << std::noshowpos
			<< " | " << std::setw(9)  << std::fixed << std::setprecision(3) << conf
			<< " | " << status);
	}

	DBG_INFO("Band Details:");
	for (std::size_t i = 1; i < m_config.getNumBands(); i++) {
		float f_start = m_bands[i].getStartFreq();
		float f_end = m_bands[i].getEndFreq();

		DBG_INFO("Band " << i
			<< ": "
			<< std::fixed << std::setprecision(0) << f_start
			<< " Hz - "
			<< std::fixed << std::setprecision(0) << f_end
			<< " Hz");
	}
	DBG_INFO("");
}

void FFTSyncCheck::processSampleBlock(const std::vector<float> &samples)
{
	// Apply Hann window to the time-domain signal
	std::fill(m_analysisTimeData.begin(), m_analysisTimeData.end(), 0.0f);
	for (std::size_t i = 0; i < m_config.getBlockSize() && i < samples.size(); i++) {
		float window_val = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (m_config.getBlockSize() - 1)));
		m_analysisTimeData[i] = samples[i] * window_val;
	}

	// Execute forward FFT on the windowed data
	kiss_fftr(m_analysisForwardCfg.get(), m_analysisTimeData.data(), m_analysisFreqData.data());

	// Calculate delays relative to band 0 using the FFT data
	for (std::size_t i = 1; i < m_config.getNumBands(); i++) {
		float delay = calculateBandDelayFFT(m_analysisFreqData, 0, i);
		m_delayStats[i].update(delay);
	}
}

float FFTSyncCheck::getFilteredDelay(std::size_t band)
{
	if (band > 0 && band < m_config.getNumBands()) {
		return m_delayStats[band].m_filteredDelay;
	}
	return 0.0f;
}

std::vector<float> FFTSyncCheck::getDelayEstimates() const
{
	std::vector<float> delays;
	for (std::size_t i = 1; i < m_config.getNumBands(); i++) {
		delays.push_back(m_delayStats[i].m_filteredDelay);
	}
	return delays;
}
