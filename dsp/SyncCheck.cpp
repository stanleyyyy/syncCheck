#include "SyncCheck.h"
#include "Logger.h"
#include <iomanip>
#include "Logger.h"
FFTSyncCheck::FFTSyncCheck(const SyncCheckConfig& config)
	: m_config(config)
	, m_bands(config.getNumBands())
	, m_delayStats(config.getNumBands())
	, m_fftProcessor(std::make_unique<KissFFTWrapper>(config.getBlockSize()))
	, m_paddedFftProcessor(std::make_unique<KissFFTWrapper>(config.getBlockSize() * config.getInterpFactor()))
	, m_windowGenerator(std::make_unique<STFTWindowGenerator>(config.getBlockSize(), config.getOverlapSize()))
	, m_processingBufferFilled(0)
	, m_processingBuffer(config.getBlockSize())
	, m_windowedBuffer(config.getBlockSize())
	, m_correlationData(config.getBlockSize() / 2 + 1)
	, m_correlationDataPadded(config.getBlockSize() * config.getInterpFactor() / 2 + 1)
	, m_paddedTimeData(config.getBlockSize() * config.getInterpFactor())
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

float FFTSyncCheck::computePSLR(const std::vector<float> &corr, int exclusion_radius)
{
	float pslr_linear = 0.0f;
	int N = static_cast<int>(corr.size());
	int main_idx = 0;
	float main_peak = 0.0f;
	// Find the index and value of the main peak (by absolute value)
	for (int i = 0; i < N; i++) {
		float abs_val = std::fabs(corr[i]);
		if (abs_val > main_peak) {
			main_peak = abs_val;
			main_idx = i;
		}
	}
	// Determine the exclusion window around the main peak
	int left = std::max(0, main_idx - exclusion_radius);
	int right = std::min(N, main_idx + exclusion_radius + 1);

	// Find the maximum absolute value outside the exclusion window (side lobe)
	float largest_side = 0.0f;
	for (int i = 0; i < left; i++) {
		float abs_val = std::fabs(corr[i]);
		if (abs_val > largest_side)
			largest_side = abs_val;
	}
	for (int i = right; i < N; i++) {
		float abs_val = std::fabs(corr[i]);
		if (abs_val > largest_side)
			largest_side = abs_val;
	}
	// Avoid division by zero; if no side lobe is found, set PSLR to infinity
	if (largest_side < 1e-12f)
		pslr_linear = std::numeric_limits<float>::infinity();
	else
		pslr_linear = main_peak / largest_side;

	// Compute PSLR in dB
	float pslr_db = std::isfinite(pslr_linear) ? 20.0f * std::log10(pslr_linear) : std::numeric_limits<float>::infinity();
	return pslr_db;
}

std::pair<float, float> FFTSyncCheck::calculateBandDelayFFT(const std::vector<kiss_fft_cpx> &fft_data, std::size_t band1, std::size_t band2)
{
	// Get configuration parameters.
	const std::size_t blockSize = m_config.getBlockSize();
	const std::size_t interpFactor = m_config.getInterpFactor();
	const std::size_t padded_size = blockSize * interpFactor;
	const std::size_t padded_freq_size = padded_size / 2 + 1;
	const std::size_t orig_freq_size = blockSize / 2 + 1;

	// Compute the normalized cross-correlation product.
	// We'll compute it for frequency bins corresponding to the band,
	// using the original FFT data (without any scaling or padding).
	m_correlationData.resize(orig_freq_size);
	std::fill(m_correlationData.begin(), m_correlationData.end(), kiss_fft_cpx{ 0.0f, 0.0f });

	// Determine band shift and width in Hz.
	const double band_shift_Hz = m_bands[band2].m_startFreq - m_bands[band1].m_startFreq;
	const double band_width_Hz = m_bands[band1].m_endFreq - m_bands[band1].m_startFreq + 1;

	// Convert to frequency-bin indices.
	const std::size_t band_shift = static_cast<std::size_t>(round(band_shift_Hz * blockSize / m_config.getSampleRate()));
	const std::size_t band_width = static_cast<std::size_t>(round(band_width_Hz * blockSize / m_config.getSampleRate()));

	// For each bin in the band, compute the normalized product:
	// product = F1(k) * conj(F2(k)) / (|F1(k)||F2(k)|)
	// This yields a unit-magnitude complex number whose phase is the phase difference.
	for (std::size_t i = 0; i < band_width; i++) {
		std::size_t bin1 = m_bands[band1].getStartBin(blockSize, m_config.getSampleRate()) + i;
		std::size_t bin2 = bin1 + band_shift;
		if (bin1 < orig_freq_size && bin2 <= m_bands[band2].getEndBin(blockSize, m_config.getSampleRate()) && bin2 < orig_freq_size) {
			float real1 = fft_data[bin1].r;
			float imag1 = fft_data[bin1].i;
			float real2 = fft_data[bin2].r;
			float imag2 = fft_data[bin2].i;

			float mag1 = sqrtf(real1 * real1 + imag1 * imag1);
			float mag2 = sqrtf(real2 * real2 + imag2 * imag2);

			if (mag1 > 1e-10f && mag2 > 1e-10f) {
				float scale = 1.0f / (mag1 * mag2);
				// Compute F1 * conj(F2):
				m_correlationData[bin1].r = (real1 * real2 + imag1 * imag2) * scale;
				m_correlationData[bin1].i = (imag1 * real2 - real1 * imag2) * scale;
			}
		}
	}

	// Create a frequency-domain buffer of the padded size and embed the product.
	// For correct time-domain interpolation, multiply by the factor (padded_size / blockSize) = interpFactor.
	// The rest of m_correlationDataPadded remains zero.
	m_correlationDataPadded.resize(padded_freq_size);
	std::fill(m_correlationDataPadded.begin(), m_correlationDataPadded.end(), kiss_fft_cpx{ 0.0f, 0.0f });

	for (std::size_t i = 0; i < orig_freq_size; i++) {
		m_correlationDataPadded[i].r = m_correlationData[i].r * interpFactor;
		m_correlationDataPadded[i].i = m_correlationData[i].i * interpFactor;
	}

	// Inverse FFT - prepare an output buffer with padded_size samples.
	if (!m_paddedFftProcessor->inverse(m_correlationDataPadded, m_paddedTimeData, true)) {
		DBG_ERROR("Inverse FFT failed.");
		return std::make_pair(0.0f, 0.0f);
	}

	// SHIFT the data so that zero-lag is in the middle => index padded_size/2 corresponds to 0 delay
	std::vector<float> shiftedData(padded_size);
	for (std::size_t i = 0; i < padded_size; i++) {
		// Shift by padded_size/2 forward
		shiftedData[i] = m_paddedTimeData[(i + padded_size/2) % padded_size];
	}
	m_paddedTimeData = shiftedData;

	// Compute PSLR (Peak-to-Side-Lobe Ratio)
	float pslr_db = computePSLR(m_paddedTimeData, padded_size / 64);

	// Write out the correlation data for debugging
	{
		static int dump_count = 0;
		if (band1 == 2) {
			char filename[256];
			snprintf(filename, sizeof(filename), "correlation_data_%zu_%zu_%05d.bin", band1, band2, dump_count);
			FILE* f = fopen(filename, "wb");
			if (f) {
				// Save parameters
				int32_t params[] = {
					static_cast<int32_t>(padded_size),
					static_cast<int32_t>(m_config.getSampleRate()),
					static_cast<int32_t>(interpFactor)
				};
				fwrite(params, sizeof(params[0]), sizeof(params) / sizeof(params[0]), f);
				// Save SHIFTED correlation data
				fwrite(m_paddedTimeData.data(), sizeof(float), padded_size, f);
				fclose(f);
			}
		}
		if (band1 == m_config.getNumBands() - 1) {
			dump_count++;
		}
	}

	// Find the peak. (Now the array is "centered"; if the true delay is zero, the max should appear near index padded_size/2.)
	float max_corr = 0.0f;
	int max_idx = 0;
	for (std::size_t i = 0; i < padded_size; i++) {
		float val = m_paddedTimeData[i];
		float abs_val = fabsf(val);
		if (abs_val > max_corr) {
			max_corr = abs_val;
			max_idx = static_cast<int>(i);
		}
	}

	// Optional search for a secondary opposite-sign peak near max_idx.
	const float PEAK_THRESHOLD = 0.5f;
	const std::size_t MAX_PEAK_SEPARATION = static_cast<std::size_t>(0.5f * m_config.getSampleRate() * interpFactor / 1000.0f);
	float main_val = m_paddedTimeData[max_idx];
	int opposite_peak_idx = -1;
	float opposite_peak_val = 0.0f;
	int start_i = std::max(0, max_idx - static_cast<int>(MAX_PEAK_SEPARATION));
	int end_i = std::min<int>(padded_size, max_idx + static_cast<int>(MAX_PEAK_SEPARATION));
	for (int i = start_i; i < end_i; i++) {
		float val = m_paddedTimeData[i];
		if (main_val * val < 0.0f && fabsf(val) > max_corr * PEAK_THRESHOLD) {
			if (fabsf(val) > fabsf(opposite_peak_val)) {
				opposite_peak_val = val;
				opposite_peak_idx = i;
			}
		}
	}

	// Merge main_idx with opposite peak index if found
	if (opposite_peak_idx >= 0) {
		if (main_val > 0.0f) {
			max_idx = static_cast<int>(roundf(0.6f * max_idx + 0.4f * opposite_peak_idx));
		} else {
			max_idx = static_cast<int>(roundf(0.4f * max_idx + 0.6f * opposite_peak_idx));
		}
	}

	// Parabolic interpolation around max_idx
	float alpha = m_paddedTimeData[(max_idx - 1 + padded_size) % padded_size];
	float beta  = m_paddedTimeData[max_idx];
	float gamma = m_paddedTimeData[(max_idx + 1) % padded_size];
	float p = 0.5f * (alpha - gamma) / (alpha - 2.0f * beta + gamma);
	float interpolated_idx = static_cast<float>(max_idx) + p;

	// Convert that index to a delay in milliseconds.
	// If "interpolated_idx" == padded_size/2 => delay = 0
	// < padded_size/2 => negative delay
	// > padded_size/2 => positive delay
	float delay = (interpolated_idx - (float)(padded_size / 2)) / (static_cast<float>(m_config.getSampleRate()) * interpFactor);
	delay *= 1000.0f; // ms
	return std::make_pair(delay, pslr_db);
}


void FFTSyncCheck::printDelayStats(const std::vector<float>& expected_delays_ms)
{
	DBG_INFO("=== Delay Statistics and Measurements ===");
	DBG_INFO("Band | Expected (ms) | Delay (ms) | Error (ms) | Confidence | Status");
	DBG_INFO("-----+--------------+------------+-----------+-----------+-------------");

	for (std::size_t i = 1; i < m_config.getNumBands(); i++) {
		float delay_ms = m_delayStats[i].m_filteredDelay;
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

void FFTSyncCheck::processSampleBlock(const std::vector<float>& samples)
{
	// make sure the processing buffer is the same size as the block size
	if (m_processingBuffer.size() != m_config.getBlockSize()) {
		m_processingBuffer.resize(m_config.getBlockSize());
	}

	std::size_t processed = 0;

	while (processed < samples.size()) {

		// Copy new samples into processing buffer
		size_t samplesToProcess = std::min(samples.size() - processed, m_config.getBlockSize() - m_processingBufferFilled);
		std::copy(samples.begin() + processed, samples.begin() + processed + samplesToProcess, m_processingBuffer.begin() + m_processingBufferFilled);
		m_processingBufferFilled += samplesToProcess;
		processed += samplesToProcess;

		// Process when we have a full block
		if (m_processingBufferFilled >= m_config.getBlockSize()) {
			// Apply analysis window
			const auto& window = m_windowGenerator->getAnalysisWindow();
			for (size_t i = 0; i < m_config.getBlockSize(); i++) {
				m_windowedBuffer[i] = m_processingBuffer[i] * window[i];
			}

			// Perform FFT
			std::vector<kiss_fft_cpx> fftData(m_config.getBlockSize() / 2 + 1);
			m_fftProcessor->forward(m_windowedBuffer, fftData);

			// Calculate delays relative to band 0 using the FFT data
			for (std::size_t i = 1; i < m_config.getNumBands(); i++) {
				auto [delay, pslr] = calculateBandDelayFFT(fftData, i, 0);
				m_delayStats[i].update(delay, pslr);
			}

			// consume 'hopsize' worth of samples from the buffer
			std::copy(m_processingBuffer.begin() + m_config.getHopSize(), m_processingBuffer.begin() + m_config.getBlockSize(), m_processingBuffer.begin());
			m_processingBufferFilled -= m_config.getHopSize();
		}
	}
}

float FFTSyncCheck::getFilteredDelay(std::size_t band)
{
	if (band > 0 && band < m_config.getNumBands()) {
		return m_delayStats[band].m_filteredDelay;
	}
	return 0.0f;
}

/**
 * @brief Get all delay statistics
 * @return Vector of tuples containing filtered delay, confidence and PSLR for each band
 */
std::vector<std::tuple<float, float, float>> FFTSyncCheck::getDelayStats() const
{
	std::vector<std::tuple<float, float, float>> delays;
	for (std::size_t i = 0; i < m_config.getNumBands(); i++) {
		delays.push_back(std::make_tuple(
			m_delayStats[i].m_filteredDelay,
			m_delayStats[i].m_confidence,
			m_delayStats[i].m_filteredPSLR
		));
	}
	return delays;
}
