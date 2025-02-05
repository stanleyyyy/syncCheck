#include "SyncCheck.h"
#include <sndfile.h>

FFTSyncCheck::BandProcessor::BandProcessor(int block_size, int overlap_size, const Band &band, int sample_rate)
	: m_forwardCfg(kiss_fftr_alloc(block_size, 0, nullptr, nullptr), free)
	, m_inverseCfg(kiss_fftr_alloc(block_size, 1, nullptr, nullptr), free)
	, m_timeData(block_size)
	, m_freqData(block_size / 2 + 1)
	, m_workingFreqData(block_size / 2 + 1)
	, m_band(band)
	, m_overlapBuffer(overlap_size, 0.0f)
	, m_blockSize(block_size)
	, m_overlapSize(overlap_size)
	, m_window(block_size, 0.0f)
	, m_inBandPower(0.0)
	, m_outBandPower(0.0)
	, m_sampleRate(sample_rate)
{
	// Square root of Hann window - perfect for 50% overlap when applied twice
	for (int i = 0; i < m_blockSize; i++) {
		float hann = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (m_blockSize - 1)));
		m_window[i] = sqrtf(hann);  // Square root for analysis/synthesis
	}
}

FFTSyncCheck::BandProcessor::BandProcessor(const BandProcessor &other)
	: m_forwardCfg(kiss_fftr_alloc(other.m_blockSize, 0, nullptr, nullptr), free)
	, m_inverseCfg(kiss_fftr_alloc(other.m_blockSize, 1, nullptr, nullptr), free)
	, m_timeData(other.m_timeData)
	, m_freqData(other.m_freqData)
	, m_workingFreqData(other.m_workingFreqData)
	, m_band(other.m_band)
	, m_overlapBuffer(other.m_overlapBuffer)
	, m_blockSize(other.m_blockSize)
	, m_overlapSize(other.m_overlapSize)
	, m_window(other.m_window)
	, m_inBandPower(other.m_inBandPower)
	, m_outBandPower(other.m_outBandPower)
	, m_sampleRate(other.m_sampleRate)
{
}

FFTSyncCheck::BandProcessor::BandProcessor(BandProcessor &&other) noexcept
	: m_forwardCfg(std::move(other.m_forwardCfg))
	, m_inverseCfg(std::move(other.m_inverseCfg))
	, m_timeData(std::move(other.m_timeData))
	, m_freqData(std::move(other.m_freqData))
	, m_workingFreqData(std::move(other.m_workingFreqData))
	, m_band(other.m_band)
	, m_overlapBuffer(std::move(other.m_overlapBuffer))
	, m_blockSize(other.m_blockSize)
	, m_overlapSize(other.m_overlapSize)
	, m_window(std::move(other.m_window))
	, m_inBandPower(other.m_inBandPower)
	, m_outBandPower(other.m_outBandPower)
	, m_sampleRate(other.m_sampleRate)
{
}

FFTSyncCheck::BandProcessor::~BandProcessor() = default;

FFTSyncCheck::BandProcessor &FFTSyncCheck::BandProcessor::operator=(const BandProcessor &other)
{
	if (this != &other) {
		m_forwardCfg.reset(kiss_fftr_alloc(other.m_blockSize, 0, nullptr, nullptr));
		m_inverseCfg.reset(kiss_fftr_alloc(other.m_blockSize, 1, nullptr, nullptr));
		m_timeData = other.m_timeData;
		m_freqData = other.m_freqData;
		m_workingFreqData = other.m_workingFreqData;
		m_band = other.m_band;
		m_overlapBuffer = other.m_overlapBuffer;
		m_blockSize = other.m_blockSize;
		m_overlapSize = other.m_overlapSize;
		m_window = other.m_window;
		m_inBandPower = other.m_inBandPower;
		m_outBandPower = other.m_outBandPower;
		m_sampleRate = other.m_sampleRate;
	}
	return *this;
}

FFTSyncCheck::BandProcessor &FFTSyncCheck::BandProcessor::operator=(BandProcessor &&other) noexcept
{
	if (this != &other) {
		m_forwardCfg = std::move(other.m_forwardCfg);
		m_inverseCfg = std::move(other.m_inverseCfg);
		m_timeData = std::move(other.m_timeData);
		m_freqData = std::move(other.m_freqData);
		m_workingFreqData = std::move(other.m_workingFreqData);
		m_band = other.m_band;
		m_overlapBuffer = std::move(other.m_overlapBuffer);
		m_blockSize = other.m_blockSize;
		m_overlapSize = other.m_overlapSize;
		m_window = std::move(other.m_window);
		m_inBandPower = other.m_inBandPower;
		m_outBandPower = other.m_outBandPower;
		m_sampleRate = other.m_sampleRate;
	}
	return *this;
}

void FFTSyncCheck::BandProcessor::processBlockFFT(const std::vector<kiss_fft_cpx>& fft_data, std::vector<float>& output)
{
	// Copy input FFT data
	std::copy(fft_data.begin(), fft_data.begin() + m_blockSize / 2 + 1, m_workingFreqData.begin());

	// Band-pass filter and measure power
	m_inBandPower = 0.0f;
	m_outBandPower = 0.0f;
	for (int i = 0; i < m_blockSize / 2 + 1; i++) {
		float real_val = m_workingFreqData[i].r;
		float imag_val = m_workingFreqData[i].i;
		float power = real_val * real_val + imag_val * imag_val;
		if (i >= m_band.m_startBin && i <= m_band.m_endBin) {
			m_inBandPower += power;
		}
		else {
			m_outBandPower += power;
			m_workingFreqData[i].r = 0.0f;
			m_workingFreqData[i].i = 0.0f;
		}
	}

	// Execute inverse FFT
	kiss_fftri(m_inverseCfg.get(), m_workingFreqData.data(), m_timeData.data());

	// Apply scaling and window
	for (int i = 0; i < m_blockSize; i++) {
		m_timeData[i] = m_timeData[i] * m_window[i] / m_blockSize;
	}

	// Overlap-add: First part combines with previous overlap
	for (int i = 0; i < m_overlapSize; i++) {
		output[i] = m_overlapBuffer[i] + m_timeData[i];
	}

	// Second part is just the current block
	for (int i = m_overlapSize; i < m_blockSize; i++) {
		output[i] = m_timeData[i];
	}

	// Save the overlap for next time
	for (int i = 0; i < m_overlapSize; i++) {
		m_overlapBuffer[i] = m_timeData[m_blockSize - m_overlapSize + i];
	}
}

void FFTSyncCheck::BandProcessor::processBlockWithShiftFFT(const std::vector<kiss_fft_cpx>& fft_data, std::vector<float>& output)
{
	// Reset power measurements
	m_inBandPower = 0.0f;
	m_outBandPower = 0.0f;

	// Zero out the destination buffer
	std::fill(m_workingFreqData.begin(), m_workingFreqData.end(), kiss_fft_cpx{0.0f, 0.0f});

	// Calculate band width
	int band_width = m_band.m_endBin - m_band.m_startBin + 1;

	// Copy and shift the lowest band (0 to band_width-1) to target position
	for (int i = 0; i < band_width; i++) {
		int target_bin = m_band.m_startBin + i;
		if (target_bin < m_blockSize / 2 + 1) {
			m_workingFreqData[target_bin] = fft_data[i];
			float power = fft_data[i].r * fft_data[i].r + fft_data[i].i * fft_data[i].i;
				m_inBandPower += power;
		}
	}

	// Calculate power for bins outside our band
	for (int i = 0; i < m_blockSize / 2 + 1; i++) {
		if (i < m_band.m_startBin || i > m_band.m_endBin) {
			float power = fft_data[i].r * fft_data[i].r + fft_data[i].i * fft_data[i].i;
				m_outBandPower += power;
		}
	}

	// Execute inverse FFT
	kiss_fftri(m_inverseCfg.get(), m_workingFreqData.data(), m_timeData.data());

	// Apply scaling and window
	for (int i = 0; i < m_blockSize; i++) {
		m_timeData[i] = m_timeData[i] * m_window[i] / m_blockSize;
	}

	// Overlap-add: First part combines with previous overlap
	for (int i = 0; i < m_overlapSize; i++) {
		output[i] = m_overlapBuffer[i] + m_timeData[i];
	}

	// Second part is just the current block
	for (int i = m_overlapSize; i < m_blockSize; i++) {
		output[i] = m_timeData[i];
	}

	// Save the overlap for next time
	for (int i = 0; i < m_overlapSize; i++) {
		m_overlapBuffer[i] = m_timeData[m_blockSize - m_overlapSize + i];
	}
}

void FFTSyncCheck::BandProcessor::processBlock(const std::vector<float>& input, std::vector<float>& output)
{
	// Apply window and copy input to FFT buffer
	for (int i = 0; i < m_blockSize; i++) {
		m_timeData[i] = input[i] * m_window[i];
	}

	// Execute forward FFT
	kiss_fftr(m_forwardCfg.get(), m_timeData.data(), m_freqData.data());

	// Process the FFT data
	processBlockFFT(m_freqData, output);
}

void FFTSyncCheck::BandProcessor::processBlockWithShift(const std::vector<float>& input, std::vector<float>& output)
{
	// Apply window and copy input to FFT buffer
	for (int i = 0; i < m_blockSize; i++) {
		m_timeData[i] = input[i] * m_window[i];
	}

	// Execute forward FFT
	kiss_fftr(m_forwardCfg.get(), m_timeData.data(), m_freqData.data());

	// Process the FFT data with shift
	processBlockWithShiftFFT(m_freqData, output);
}

float FFTSyncCheck::BandProcessor::getBandIsolation() const
{
	return 10.0f * log10f(m_inBandPower / (m_outBandPower + 1e-10f));
}

void FFTSyncCheck::BandProcessor::applyWindow(const std::vector<float>& input, std::vector<float>& output, int size) const
{
	for (int i = 0; i < size; i++) {
		output[i] = input[i] * m_window[i];
	}
}

FFTSyncCheck::FFTSyncCheck(int fft_size, int sample_rate)
	: m_sampleRate(sample_rate)
	, m_fftSize(fft_size)
	, m_bands(NUM_BANDS)
	, m_delayStats(NUM_BANDS)
	, m_analysisForwardCfg(kiss_fftr_alloc(fft_size, 0, nullptr, nullptr), free)
	, m_analysisInverseCfg(kiss_fftr_alloc(fft_size, 1, nullptr, nullptr), free)
	, m_analysisTimeData(fft_size)
	, m_analysisFreqData(fft_size / 2 + 1)
	, m_correlationData(fft_size / 2 + 1)
	, m_paddedForwardCfg(kiss_fftr_alloc(fft_size * INTERP_FACTOR, 0, nullptr, nullptr), free)
	, m_paddedInverseCfg(kiss_fftr_alloc(fft_size * INTERP_FACTOR, 1, nullptr, nullptr), free)
	, m_paddedTimeData(fft_size * INTERP_FACTOR)
	, m_paddedCorrData(fft_size * INTERP_FACTOR / 2 + 1)
	, m_paddedFreqData(fft_size * INTERP_FACTOR / 2 + 1)
{
	// Calculate band boundaries.
	int bins_per_band = (m_fftSize / 2) / NUM_BANDS;
	for (int i = 0; i < NUM_BANDS; i++) {
		m_bands[i].m_startBin = i * bins_per_band;
		m_bands[i].m_endBin = (i + 1) * bins_per_band - 1;
	}
}

FFTSyncCheck::~FFTSyncCheck() = default;

void FFTSyncCheck::extractBand(const std::vector<kiss_fft_cpx>& input, std::vector<kiss_fft_cpx>& output, int band_idx)
{
	std::fill(output.begin(), output.end(), kiss_fft_cpx{0.0f, 0.0f});
	if (band_idx >= 0 && band_idx < NUM_BANDS) {
		int start = m_bands[band_idx].m_startBin;
		int end = m_bands[band_idx].m_endBin;
		for (int i = start; i <= end; i++) {
			output[i] = input[i];
		}
	}
}

float FFTSyncCheck::calculateBandDelayFFT(const std::vector<kiss_fft_cpx>& fft_data, int band1, int band2)
{
	const int padded_size = m_fftSize * INTERP_FACTOR;
	const int padded_freq_size = padded_size / 2 + 1;
	const int orig_freq_size = m_fftSize / 2 + 1;

	// Zero out the padded frequency buffer.
	std::fill(m_paddedFreqData.begin(), m_paddedFreqData.end(), kiss_fft_cpx{0.0f, 0.0f});

	// Copy the original FFT data into the padded buffer with scaling.
	for (int i = 0; i < orig_freq_size; i++) {
		m_paddedFreqData[i].r = fft_data[i].r * INTERP_FACTOR;
		m_paddedFreqData[i].i = fft_data[i].i * INTERP_FACTOR;
	}

	// Determine band shift and width (in original frequency bins).
	int band_shift = m_bands[band2].m_startBin - m_bands[band1].m_startBin;
	int band_width = m_bands[band1].m_endBin - m_bands[band1].m_startBin + 1;

	// Zero out the padded correlation buffer.
	std::fill(m_paddedCorrData.begin(), m_paddedCorrData.end(), kiss_fft_cpx{0.0f, 0.0f});

	// Compute the normalized cross-correlation product over the band.
	for (int i = 0; i < band_width; i++) {
		int bin1 = m_bands[band1].m_startBin + i;
		int bin2 = bin1 + band_shift;

		if (bin2 >= 0 && bin2 <= m_bands[band2].m_endBin &&
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
				real1 /= mag1;
				imag1 /= mag1;
				real2 /= mag2;
				imag2 /= mag2;

				// Store the normalized cross-correlation product in the padded buffer.
				m_paddedCorrData[bin1].r = real2 * real1 + imag2 * imag1;
				m_paddedCorrData[bin1].i = imag2 * real1 - real2 * imag1;
			}
		}
	}

	// Perform inverse FFT on the zero-padded cross-correlation product.
	kiss_fftri(m_paddedInverseCfg.get(), m_paddedCorrData.data(), m_paddedTimeData.data());

	// Find the index of the maximum correlation peak.
	float max_corr = 0.0f;
	int max_idx = 0;
	for (int i = 0; i < padded_size; i++) {
		float abs_val = fabsf(m_paddedTimeData[i]);
		if (abs_val > max_corr) {
			max_corr = abs_val;
			max_idx = i;
		}
	}

	// Apply parabolic interpolation for sub-sample accuracy.
	float alpha = m_paddedTimeData[(max_idx + padded_size - 1) % padded_size];
	float beta = m_paddedTimeData[max_idx];
	float gamma = m_paddedTimeData[(max_idx + 1) % padded_size];

	// p will be in range [-0.5, 0.5], indicating the fractional sample offset
	float p = 0.5f * (alpha - gamma) / (alpha - 2.0f * beta + gamma);
	float interpolated_idx = max_idx + p;

	// Convert the index to a time delay (accounting for the interpolation factor).
	float delay = 0.0f;
	if (interpolated_idx > padded_size / 2) {
		delay = -(padded_size - interpolated_idx) / (static_cast<float>(m_sampleRate) * INTERP_FACTOR);
	} else {
		delay = interpolated_idx / (static_cast<float>(m_sampleRate) * INTERP_FACTOR);
	}

	return delay;
}

void FFTSyncCheck::printDelayStats()
{
	printf("\n=== Delay Statistics (reference: Band 0) ===\n");
	printf("Band | Delay (ms) | Confidence | Status\n");
	printf("-----+------------+------------+--------\n");
	for (int i = 1; i < NUM_BANDS; i++) {
		float delay_ms = m_delayStats[i].m_filteredDelay * 1000.0f;
		float conf = m_delayStats[i].m_confidence;
		const char *status = "OK";
		if (conf < 0.5f)
			status = "UNRELIABLE";
		if (fabs(delay_ms) > 10.0f)
			status = "HIGH DELAY";
		printf("%4d | %+9.3f | %9.2f | %s\n", i, delay_ms, conf, status);
	}

	printf("\nBand Details:\n");
	for (int i = 1; i < NUM_BANDS; i++) {
		float f_start = m_bands[i].m_startBin * static_cast<float>(m_sampleRate) / m_fftSize;
		float f_end = m_bands[i].m_endBin * static_cast<float>(m_sampleRate) / m_fftSize;
		printf("Band %d: %.0f Hz - %.0f Hz\n", i, f_start, f_end);
	}
	printf("\n");
}

void FFTSyncCheck::processSampleBlock(const std::vector<float> &samples)
{
	// Apply Hann window to the time-domain signal
	std::fill(m_analysisTimeData.begin(), m_analysisTimeData.end(), 0.0f);
	for (int i = 0; i < m_fftSize && i < (int)samples.size(); i++) {
		float window_val = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (m_fftSize - 1)));
		m_analysisTimeData[i] = samples[i] * window_val;
	}

	// Execute forward FFT on the windowed data
	kiss_fftr(m_analysisForwardCfg.get(), m_analysisTimeData.data(), m_analysisFreqData.data());

	// Calculate delays relative to band 0 using the FFT data
	for (int i = 1; i < NUM_BANDS; i++) {
		float delay = calculateBandDelayFFT(m_analysisFreqData, 0, i);
		m_delayStats[i].update(delay);
	}
}

float FFTSyncCheck::getFilteredDelay(int band)
{
	if (band > 0 && band < NUM_BANDS) {
		return m_delayStats[band].m_filteredDelay;
	}
	return 0.0f;
}

std::vector<float> FFTSyncCheck::generateTestSignal(float duration, std::vector<float> &delays_ms, bool verbose)
{
	if (delays_ms.size() != NUM_BANDS) {
		throw std::runtime_error("Delays array must match number of bands");
	}

	const int OVERLAP_SIZE = FFTSyncCheck::BLOCK_SIZE / 2;
	const int HOP_SIZE = FFTSyncCheck::BLOCK_SIZE - OVERLAP_SIZE;

	int num_samples = static_cast<int>(duration * m_sampleRate);
	std::vector<float> reference_signal(num_samples);
	std::vector<float> mixed_signal(num_samples, 0.0f);
	std::vector<std::vector<float>> band_signals(NUM_BANDS, std::vector<float>(num_samples, 0.0f));

	// Generate reference signal with a very small correlation (White noise)
	{
		std::random_device rd;
		std::mt19937 gen(rd());
		std::uniform_real_distribution<float> dist(-0.5f, 0.5f);

		// Generate white noise reference signal
		for (int i = 0; i < num_samples; i++) {
			reference_signal[i] = dist(gen);
		}
	}

	// Create band processors
	std::vector<BandProcessor> processors;
	for (int band = 0; band < NUM_BANDS; band++) {
		processors.emplace_back(FFTSyncCheck::BLOCK_SIZE, OVERLAP_SIZE, m_bands[band], m_sampleRate);
	}

	if (verbose) {
		printf("\nGenerating test signal:\n");
		printf("[");
	}
	int last_percent = 0;

	// Process each block
	for (int offset = 0; offset < num_samples; offset += HOP_SIZE) {
		if (verbose) {
			int percent = (offset * 100) / num_samples;
			if (percent > last_percent) {
				for (int i = last_percent; i < percent; i += 2) {
					printf("=");
					fflush(stdout);
				}
				last_percent = percent;
			}
		}

		// Prepare input block
		std::vector<float> input_block(BLOCK_SIZE, 0.0f);
		std::vector<float> block_output(BLOCK_SIZE);
		int samples_to_copy = std::min(BLOCK_SIZE, num_samples - offset);
		std::copy(reference_signal.begin() + offset,
				 reference_signal.begin() + offset + samples_to_copy,
				 input_block.begin());

		// Process each band
		for (int band = 0; band < NUM_BANDS; band++) {
			processors[band].processBlockWithShift(input_block, block_output);

			// Store band signal and add to mixed output with appropriate delay
			int valid_samples = std::min(HOP_SIZE, num_samples - offset);
			int delay_samples = static_cast<int>(roundf(delays_ms[band] * m_sampleRate / 1000));

			// recalculate actual delay
			delays_ms[band] = 1000.0f * delay_samples / m_sampleRate;

			for (int i = 0; i < valid_samples; i++) {
				int out_idx = offset + i;
				band_signals[band][out_idx] = block_output[i];

				int delayed_idx = out_idx + delay_samples;
				if (delayed_idx >= 0 && delayed_idx < num_samples) {
					mixed_signal[delayed_idx] += block_output[i] / NUM_BANDS;
				}
			}
		}
	}

	if (verbose) {
		for (int i = last_percent; i <= 100; i += 2) {
			printf("=");
		}
		printf("] 100%%\n\n");
	}

	// Save individual band signals and mixed signal to WAV files
	SF_INFO sf_info;
	sf_info.samplerate = m_sampleRate;
	sf_info.channels = 1;
	sf_info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
	sf_info.sections = 1;
	sf_info.seekable = 1;

	// Save individual band signals
	for (int band = 0; band < NUM_BANDS; band++) {
		char filename[256];
		snprintf(filename, sizeof(filename), "band_%d.wav", band);
		SNDFILE* file = sf_open(filename, SFM_WRITE, &sf_info);
		if (file) {
			sf_writef_float(file, band_signals[band].data(), num_samples);
			sf_close(file);
		}
	}

	// Save mixed signal
	SNDFILE* mixed_file = sf_open("mixed_signal.wav", SFM_WRITE, &sf_info);
	if (mixed_file) {
		sf_writef_float(mixed_file, mixed_signal.data(), num_samples);
		sf_close(mixed_file);
	}

	// Calculate quality metrics
	QualityMetrics metrics;
	float total_isolation = 0.0f;
	for (int band = 0; band < NUM_BANDS; band++) {
		total_isolation += processors[band].getBandIsolation();
	}
	metrics.m_bandIsolation = total_isolation / NUM_BANDS;

	float signal_power = 0.0f;
	float noise_power = 1e-10f; // Small constant to avoid division by zero
	for (int band = 0; band < NUM_BANDS; band++) {
		for (size_t i = 0; i < band_signals[band].size(); i++) {
			signal_power += band_signals[band][i] * band_signals[band][i];
		}
	}
	metrics.m_snr = 10.0f * log10f(signal_power / noise_power);

	float total_coherence = 0.0f;
	const int CORR_WINDOW = FFTSyncCheck::BLOCK_SIZE;
	for (int band = 0; band < NUM_BANDS - 1; band++) {
		float max_correlation = 0.0f;
		int start_idx = num_samples / 4;
		int end_idx = 3 * num_samples / 4;
		for (int lag = -50; lag <= 50; lag++) {
			float correlation = 0.0f;
			int count = 0;
			for (int i = start_idx; i < end_idx; i++) {
				int j = i + lag;
				if (j >= 0 && j < num_samples) {
					correlation += band_signals[band][i] * band_signals[band + 1][j];
					count++;
				}
			}
			if (count > 0) {
				correlation /= count;
				max_correlation = std::max(max_correlation, fabsf(correlation));
			}
		}
		total_coherence += max_correlation;
	}
	metrics.m_phaseCoherence = total_coherence / (NUM_BANDS - 1);

	if (verbose) {
		metrics.print();
	}

	return mixed_signal;
}