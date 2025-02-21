#include "SyncCheck.h"
#include "Logger.h"
#include <iomanip>
#include <cmath>
#include <algorithm>

FFTSyncCheck::FFTSyncCheck(const SyncCheckConfig& config)
	: m_config(config)
	, m_bands(config.getNumBands())
	, m_fftProcessor(std::make_unique<KissFFTWrapper>(config.getBlockSize()))
	, m_paddedFftProcessor(std::make_unique<KissFFTWrapper>(config.getBlockSize() * config.getInterpFactor()))
	, m_windowGenerator(std::make_unique<STFTWindowGenerator>(config.getBlockSize(), config.getOverlapSize()))
	, m_correlationData(config.getBlockSize() / 2 + 1)
	, m_correlationDataPadded(config.getBlockSize() * config.getInterpFactor() / 2 + 1)
	, m_paddedTimeData(config.getBlockSize() * config.getInterpFactor())
{
	// Use bands from config
	m_bands = config.getBands();

	// Configure Butterworth lowpass filter for correlation data filtering.
	// Cutoff frequency is 1/100 of nyquist frequency.
	m_lowpassFilter = std::make_unique<ButterworthLowpass>(2, 0.01);

	// By default we will use just a single input channel
	setMultiChannelMode(1);

	// Create default channel assignment for single-channel mode if no assignment is set.
	std::vector<ChannelAssignment> assignments;
	for (std::size_t band = 0; band < m_config.getNumBands(); band++) {
		ChannelAssignment assign;
		assign.refChannel = 0;
		assign.refBand = 0;
		assign.sigChannel = 0;
		assign.sigBand = band;
		assignments.push_back(assign);
	}
	setChannelAssignments(assignments);

	// Initialize the common fill level.
	m_processingBufferFilled = 0;
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

std::vector<float> FFTSyncCheck::getSmoothEnvelope(const std::vector<float> &data)
{
	// Ensure the lowpass filter has been initialized.
	if (!m_lowpassFilter) {
		DBG_ERROR("Lowpass filter not initialized");
		return data;
	}

	// The envelope of a signal represents its amplitude variation over time.
	// Typically, the envelope is obtained by taking the absolute value of the signal.
	//
	// Without taking the absolute value, the signal remains bipolar (i.e. oscillates between positive and negative values).
	// Filtering a bipolar signal directly—especially with a zero-phase (forward-reverse) approach—can lead to cancellation effects
	// or instability at the filter's boundaries. In particular, the two-pass filtfilt method is sensitive to the phase and sign
	// of the input. Oscillations that cross zero may produce large transients or boundary artifacts that can cause the filter to
	// fail or produce NaNs.
	//
	// By taking the absolute value, the signal is converted to a unipolar one. This emphasizes the amplitude (or energy)
	// variations and removes the rapid sign changes. The result is a smoother signal that is much easier for the low-pass filter
	// to process reliably without numerical instability.
	std::vector<float> absData(data.size());
	for (size_t i = 0; i < data.size(); i++) {
		absData[i] = std::abs(data[i]);
	}

	// Apply zero-phase filtering using the lowpass filter.
	// This forward-reverse filtering (filtfilt) helps to eliminate phase distortions.
	m_lowpassFilter->filtfilt(absData);
	return absData;
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

std::pair<float, float> FFTSyncCheck::calculateBandDelayFFT(const std::vector<kiss_fft_cpx> &fft_data_ref, const std::vector<kiss_fft_cpx> &fft_data_sig,
															std::size_t refBand, std::size_t sigBand)
{
	// Get configuration parameters.
	const std::size_t blockSize = m_config.getBlockSize();
	const std::size_t interpFactor = m_config.getInterpFactor();
	const std::size_t padded_size = blockSize * interpFactor;
	const std::size_t padded_freq_size = padded_size / 2 + 1;
	const std::size_t orig_freq_size = blockSize / 2 + 1;

	// Determine band start bins and band width (using reference band width).
	std::size_t ref_start = m_config.getBands()[refBand].getStartBin(blockSize, m_config.getSampleRate());
	std::size_t sig_start = m_config.getBands()[sigBand].getStartBin(blockSize, m_config.getSampleRate());
	std::size_t ref_end = m_config.getBands()[refBand].getEndBin(blockSize, m_config.getSampleRate());
	std::size_t band_width = ref_end - ref_start + 1;

	ASSERT_WITH_ERROR(ref_start < orig_freq_size, "Invalid reference start bin");
	ASSERT_WITH_ERROR(sig_start < orig_freq_size, "Invalid signal start bin");
	ASSERT_WITH_ERROR(ref_end < orig_freq_size, "Invalid reference end bin");
	ASSERT_WITH_ERROR(band_width > 0, "Invalid band width");

	// Clear and prepare correlation data.
	m_correlationData.resize(orig_freq_size);
	std::fill(m_correlationData.begin(), m_correlationData.end(), kiss_fft_cpx { 0.0f, 0.0f });

	// Compute the normalized cross-correlation product for the band.
	for (std::size_t i = 0; i < band_width; i++) {
		std::size_t bin_ref = ref_start + i;
		std::size_t bin_sig = sig_start + i;
		if (bin_ref < orig_freq_size && bin_sig < orig_freq_size) {
			float real_ref = fft_data_ref[bin_ref].r;
			float imag_ref = fft_data_ref[bin_ref].i;
			float real_sig = fft_data_sig[bin_sig].r;
			float imag_sig = fft_data_sig[bin_sig].i;

			float mag_ref = sqrtf(real_ref * real_ref + imag_ref * imag_ref);
			float mag_sig = sqrtf(real_sig * real_sig + imag_sig * imag_sig);

			if (mag_ref > 1e-10f && mag_sig > 1e-10f) {
				float scale = 1.0f / (mag_ref * mag_sig);
				m_correlationData[bin_ref].r = (real_ref * real_sig + imag_ref * imag_sig) * scale;
				// Multiply the imaginary component by -1 to reverse the delay sign.
				m_correlationData[bin_ref].i = -(imag_ref * real_sig - real_ref * imag_sig) * scale;
			}
		}
	}

	// Prepare padded frequency domain buffer.
	m_correlationDataPadded.resize(padded_freq_size);
	std::fill(m_correlationDataPadded.begin(), m_correlationDataPadded.end(), kiss_fft_cpx { 0.0f, 0.0f });
	for (std::size_t i = 0; i < orig_freq_size; i++) {
		m_correlationDataPadded[i].r = m_correlationData[i].r * interpFactor;
		m_correlationDataPadded[i].i = m_correlationData[i].i * interpFactor;
	}

	// Inverse FFT - prepare an output buffer with padded_size samples.
	if (!m_paddedFftProcessor->inverse(m_correlationDataPadded, m_paddedTimeData, true)) {
		DBG_ERROR("Inverse FFT failed.");
		return std::make_pair(0.0f, 0.0f);
	}

	// SHIFT the data so that zero-lag is in the middle.
	std::vector<float> shiftedData(padded_size);
	for (std::size_t i = 0; i < padded_size; i++) {
		shiftedData[i] = m_paddedTimeData[(i + padded_size / 2) % padded_size];
	}
	m_paddedTimeData = shiftedData;

	// Optionally apply lowpass smoothing to the envelope.
	bool lowPassFilter = true;
	if (lowPassFilter)
		m_paddedTimeData = getSmoothEnvelope(m_paddedTimeData);

	// Compute PSLR (Peak-to-Side-Lobe Ratio).
	float pslr_db = computePSLR(m_paddedTimeData, padded_size / 64);

	// Write out the correlation data for debugging if enabled
	if (m_config.getCorrelationDumpEnabled()) {
		char filename[256];
		snprintf(filename, sizeof(filename), "correlation_data_%zu_%zu_%05d.bin", refBand, sigBand, m_dumpCount);
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

	// Find the peak. (Now the array is "centered".)
	float max_corr = 0.0f;
	int max_idx = 0;
	for (std::size_t i = 0; i < padded_size; i++) {
		float val = m_paddedTimeData[i];
		if (fabsf(val) > max_corr) {
			max_corr = fabsf(val);
			max_idx = static_cast<int>(i);
		}
	}

	// Optional search for a secondary opposite-sign peak near max_idx.
	if (!lowPassFilter) {
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
		if (opposite_peak_idx >= 0) {
			if (main_val > 0.0f)
				max_idx = static_cast<int>(roundf(0.6f * max_idx + 0.4f * opposite_peak_idx));
			else
				max_idx = static_cast<int>(roundf(0.4f * max_idx + 0.6f * opposite_peak_idx));
		}
	}

	// Parabolic interpolation around max_idx.
	float alpha = m_paddedTimeData[(max_idx - 1 + padded_size) % padded_size];
	float beta = m_paddedTimeData[max_idx];
	float gamma = m_paddedTimeData[(max_idx + 1) % padded_size];
	float p = 0.5f * (alpha - gamma) / (alpha - 2.0f * beta + gamma);
	float interpolated_idx = static_cast<float>(max_idx) + p;

	// Convert index to delay in milliseconds.
	float delay = (interpolated_idx - (float)(padded_size / 2)) / (static_cast<float>(m_config.getSampleRate()) * interpFactor);
	delay *= 1000.0f; // ms

	return std::make_pair(delay, pslr_db);
}

/**
 * @brief Print comprehensive delay statistics for each channel assignment.
 *
 * This function now outputs the channel assignment details (reference and signal channels and bands)
 * along with the expected delay, measured delay, error and confidence.
 *
 * @param expected_delays_ms Optional vector of expected delays in milliseconds for each channel assignment.
 */
void FFTSyncCheck::printDelayStats(const std::vector<float>& expected_delays_ms)
{
	DBG_INFO("=== Delay Statistics and Measurements ===");
	DBG_INFO("Delay channel[band] -> channel[band] (ms) | Expected (ms) | Delay (ms) | Error (ms) | Confidence | Status");
	DBG_INFO("-----------------------------------------------+---------------+------------+------------+------------+-------------");

	for (std::size_t i = 0; i < m_delayStats.size(); i++) {

		// Skip if the assignment is to the same channel and band.
		const auto &assignment = m_channelAssignments[i];
		if (assignment.refChannel == assignment.sigChannel && assignment.refBand == assignment.sigBand) {
			continue;
		}

		float delay_ms = m_delayStats[i].m_filteredDelay;
		float conf = m_delayStats[i].m_confidence;
		const char *status = "OK         ";
		if (conf < 0.5f)
			status = "UNRELIABLE ";
		float expected = 0.0f;
		float error = delay_ms;
		if (!expected_delays_ms.empty() && i < expected_delays_ms.size()) {
			expected = expected_delays_ms[i];
			error = delay_ms - expected;
		}

		std::string assignmentStr = "N/A";
		if (i < m_channelAssignments.size()) {
			const auto &assignment = m_channelAssignments[i];
			assignmentStr = ""
					+ std::to_string(assignment.refChannel) + "["
					+ std::to_string(assignment.refBand) + "] -> "
					+ std::to_string(assignment.sigChannel) + "["
					+ std::to_string(assignment.sigBand) + "]";
		}

		DBG_INFO(std::setw(40) << assignmentStr
			<< " | " << std::showpos << std::setw(13) << std::fixed << std::setprecision(3) << expected
			<< " | " << std::showpos << std::setw(10) << std::fixed << std::setprecision(3) << delay_ms
			<< " | " << std::showpos << std::setw(10) << std::fixed << std::setprecision(3) << error << std::noshowpos
			<< " | " << std::setw(10) << std::fixed << std::setprecision(3) << conf
			<< " | " << status);
	}

	DBG_INFO("");
}

void FFTSyncCheck::processSampleBlock(const std::vector<float>& samples)
{
	// Ensure the number of samples is a multiple of the number of channels.
	assert((samples.size() % m_numInputChannels) == 0);

	// 'samples' are assumed to be interleaved across m_numInputChannels.
	const std::size_t blockSize     = m_config.getBlockSize();
	const std::size_t numFrames     = samples.size() / m_numInputChannels;
	const std::size_t desiredCapacity = blockSize * 2;

	// Ensure each per–channel processing buffer has sufficient capacity.
	for (std::size_t ch = 0; ch < m_numInputChannels; ch++) {
		if (m_channelProcessingBuffers[ch].size() < desiredCapacity) {
			m_channelProcessingBuffers[ch].resize(desiredCapacity, 0.0f);
		}
	}

	std::size_t processedFrames = 0;
	// Process the input frames in a loop.
	while (processedFrames < numFrames) {
		// Determine how many new frames we can copy without overrunning a block.
		std::size_t framesToCopy = std::min(numFrames - processedFrames, blockSize - m_processingBufferFilled);

		// Deinterleave and copy the frames into each channel's buffer.
		for (std::size_t ch = 0; ch < m_numInputChannels; ch++) {
			for (std::size_t i = 0; i < framesToCopy; i++) {
				m_channelProcessingBuffers[ch][m_processingBufferFilled + i] =
					samples[(processedFrames + i) * m_numInputChannels + ch];
			}
		}
		m_processingBufferFilled += framesToCopy;
		processedFrames += framesToCopy;

		// Process full blocks as long as there is enough data.
		while (m_processingBufferFilled >= blockSize) {
			// Apply the analysis window and compute the FFT for each channel used in processing.
			const auto &window = m_windowGenerator->getAnalysisWindow();
			for (std::size_t ch = 0; ch < m_numInputChannels; ch++) {
				// Only process channels that are used in one or more channel assignments.
				bool channelUsed = false;
				for (const auto &assign : m_channelAssignments) {
					if (assign.refChannel == ch || assign.sigChannel == ch) {
						channelUsed = true;
						break;
					}
				}
				if (!channelUsed)
					continue;

				// Apply the analysis window to the first 'blockSize' samples.
				for (std::size_t i = 0; i < blockSize; i++) {
					m_tempWindowedBuffers[ch][i] = m_channelProcessingBuffers[ch][i] * window[i];
				}
				// Compute FFT on the windowed data. The per–channel FFT output is stored in m_channelFFTs[ch].
				m_fftProcessor->forward(m_tempWindowedBuffers[ch], m_channelFFTs[ch]);
			}

			// Process delay estimation for each channel assignment.
			for (std::size_t i = 0; i < m_channelAssignments.size(); i++) {
				const auto &assign = m_channelAssignments[i];

				// if makes no sense to calculate cross-correlation for the same channel and band
				if (assign.refChannel == assign.sigChannel && assign.refBand == assign.sigBand) {
					m_delayStats[i].update(0.0f, 0.0f);
					continue;
				}

				// Get FFT data for the reference and signal channels.
				const auto &fftDataRef = m_channelFFTs[assign.refChannel];
				const auto &fftDataSig = m_channelFFTs[assign.sigChannel];

				// Calculate delay and PSLR using the chosen bands (assign.refBand and assign.sigBand).
				// (Assumes calculateBandDelayFFT() is appropriately overloaded.)
				auto [delay, pslr] = calculateBandDelayFFT(fftDataRef, fftDataSig, assign.refBand, assign.sigBand);
				m_delayStats[i].update(delay, pslr);
			}

			if (m_config.getCorrelationDumpEnabled())
				m_dumpCount++;

			// Remove 'hopSize' worth of samples from the beginning of each channel's processing buffer.
			std::size_t hop = m_config.getHopSize();
			for (std::size_t ch = 0; ch < m_numInputChannels; ch++) {
				std::move(m_channelProcessingBuffers[ch].begin() + hop,
				          m_channelProcessingBuffers[ch].begin() + m_processingBufferFilled,
				          m_channelProcessingBuffers[ch].begin());
			}
			m_processingBufferFilled -= hop;
		}
	}
}

float FFTSyncCheck::getFilteredDelay(std::size_t assignment)
{
	if (assignment < m_delayStats.size()) {
		return m_delayStats[assignment].m_filteredDelay;
	}
	return 0.0f;
}

/**
 * @brief Get all delay statistics together with the channel assignment details.
 *
 * Each returned tuple contains:
 *   - ChannelAssignment: details of the channel assignment (refChannel, refBand, sigChannel, sigBand)
 *   - Filtered delay (float)
 *   - Confidence (float)
 *   - Filtered PSLR (float)
 *
 * This makes it easier to link each delay measurement to its original channel assignment.
 *
 * @return Vector of tuples with channel assignment and delay statistics.
 */
std::vector<std::tuple<FFTSyncCheck::ChannelAssignment, float, float, float>> FFTSyncCheck::getDelayStats() const
{
	std::vector<std::tuple<ChannelAssignment, float, float, float>> delays;
	delays.reserve(m_delayStats.size());
	for (std::size_t i = 0; i < m_delayStats.size(); i++) {
		ChannelAssignment assignment;
		if (i < m_channelAssignments.size()) {
			assignment = m_channelAssignments[i];
		}
		delays.emplace_back(assignment, m_delayStats[i].m_filteredDelay, m_delayStats[i].m_confidence, m_delayStats[i].m_filteredPSLR);
	}
	return delays;
}

void FFTSyncCheck::setMultiChannelMode(const std::size_t numChannels)
{
	// m_multiChannelMode = enabled;
	m_numInputChannels = numChannels;

	// Initialize per-channel processing buffers.
	const size_t capacity = m_config.getBlockSize() * 2;
	m_channelProcessingBuffers.resize(m_numInputChannels);
	m_tempWindowedBuffers.resize(m_numInputChannels);
	m_channelFFTs.resize(m_numInputChannels);
	for (std::size_t ch = 0; ch < m_numInputChannels; ch++) {
		m_channelProcessingBuffers[ch].resize(capacity, 0.0f);
		m_tempWindowedBuffers[ch].resize(m_config.getBlockSize(), 0.0f);
		m_channelFFTs[ch].resize(m_config.getBlockSize() / 2 + 1);
	}
}

void FFTSyncCheck::setChannelAssignments(const std::vector<ChannelAssignment>& assignments)
{
	m_channelAssignments = assignments;
	// Resize delay statistics vector to match the number of channel assignments.
	m_delayStats.resize(assignments.size());
}
