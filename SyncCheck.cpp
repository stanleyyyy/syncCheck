#include "SyncCheck.h"
#include <sndfile.h>

FFTSyncCheck::BandProcessor::BandProcessor(int block_size, int overlap_size, const Band &band, int sample_rate)
	: m_forwardCfg(nullptr)
	, m_inverseCfg(nullptr)
	, m_timeData(nullptr)
	, m_freqData(nullptr)
	, m_band(band)
	, m_overlapBuffer(overlap_size, 0.0f)
	, m_blockSize(block_size)
	, m_overlapSize(overlap_size)
	, m_window(block_size, 0.0f)
	, m_inBandPower(0.0)
	, m_outBandPower(0.0)
	, m_sampleRate(sample_rate)
{
	allocateResources();

	// Square root of Hann window - perfect for 50% overlap when applied twice
	for (int i = 0; i < m_blockSize; i++) {
		float hann = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (m_blockSize - 1)));
		m_window[i] = sqrtf(hann);  // Square root for analysis/synthesis
	}
}

FFTSyncCheck::BandProcessor::BandProcessor(const BandProcessor &other)
	: m_forwardCfg(nullptr)
	, m_inverseCfg(nullptr)
	, m_timeData(nullptr)
	, m_freqData(nullptr)
	, m_band(other.m_band)
	, m_overlapBuffer(other.m_overlapBuffer)
	, m_blockSize(other.m_blockSize)
	, m_overlapSize(other.m_overlapSize)
	, m_window(other.m_window)
	, m_inBandPower(other.m_inBandPower)
	, m_outBandPower(other.m_outBandPower)
	, m_sampleRate(other.m_sampleRate)
{
	allocateResources();
}

FFTSyncCheck::BandProcessor::BandProcessor(BandProcessor &&other) noexcept
	: m_forwardCfg(other.m_forwardCfg)
	, m_inverseCfg(other.m_inverseCfg)
	, m_timeData(other.m_timeData)
	, m_freqData(other.m_freqData)
	, m_band(other.m_band)
	, m_overlapBuffer(std::move(other.m_overlapBuffer))
	, m_blockSize(other.m_blockSize)
	, m_overlapSize(other.m_overlapSize)
	, m_window(std::move(other.m_window))
	, m_inBandPower(other.m_inBandPower)
	, m_outBandPower(other.m_outBandPower)
	, m_sampleRate(other.m_sampleRate)
{
	other.m_forwardCfg = nullptr;
	other.m_inverseCfg = nullptr;
	other.m_timeData = nullptr;
	other.m_freqData = nullptr;
}

FFTSyncCheck::BandProcessor::~BandProcessor()
{
	freeResources();
}

FFTSyncCheck::BandProcessor &FFTSyncCheck::BandProcessor::operator=(const BandProcessor &other)
{
	if (this != &other) {
		freeResources();
		m_band = other.m_band;
		m_overlapBuffer = other.m_overlapBuffer;
		m_blockSize = other.m_blockSize;
		m_overlapSize = other.m_overlapSize;
		m_window = other.m_window;
		m_inBandPower = other.m_inBandPower;
		m_outBandPower = other.m_outBandPower;
		m_sampleRate = other.m_sampleRate;
		allocateResources();
	}
	return *this;
}

FFTSyncCheck::BandProcessor &FFTSyncCheck::BandProcessor::operator=(BandProcessor &&other) noexcept
{
	if (this != &other) {
		freeResources();
		m_forwardCfg = other.m_forwardCfg;
		m_inverseCfg = other.m_inverseCfg;
		m_timeData = other.m_timeData;
		m_freqData = other.m_freqData;
		m_band = other.m_band;
		m_overlapBuffer = std::move(other.m_overlapBuffer);
		m_blockSize = other.m_blockSize;
		m_overlapSize = other.m_overlapSize;
		m_window = std::move(other.m_window);
		m_inBandPower = other.m_inBandPower;
		m_outBandPower = other.m_outBandPower;
		m_sampleRate = other.m_sampleRate;
		other.m_forwardCfg = nullptr;
		other.m_inverseCfg = nullptr;
		other.m_timeData = nullptr;
		other.m_freqData = nullptr;
	}
	return *this;
}

void FFTSyncCheck::BandProcessor::processBlockFFT(const kiss_fft_cpx* fft_data, float* output)
{
	// Copy input FFT data
	std::memcpy(m_freqData, fft_data, sizeof(kiss_fft_cpx) * (m_blockSize / 2 + 1));

	// Band-pass filter and measure power
	m_inBandPower = 0.0f;
	m_outBandPower = 0.0f;
	for (int i = 0; i < m_blockSize / 2 + 1; i++) {
		float real_val = m_freqData[i].r;
		float imag_val = m_freqData[i].i;
		float power = real_val * real_val + imag_val * imag_val;
		if (i >= m_band.m_startBin && i <= m_band.m_endBin) {
			m_inBandPower += power;
		}
		else {
			m_outBandPower += power;
			m_freqData[i].r = 0.0f;
			m_freqData[i].i = 0.0f;
		}
	}

	// Execute inverse FFT
	kiss_fftri(m_inverseCfg, m_freqData, m_timeData);

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

void FFTSyncCheck::BandProcessor::processBlockWithShiftFFT(const kiss_fft_cpx* fft_data, float* output)
{
	// Reset power measurements
	m_inBandPower = 0.0f;
	m_outBandPower = 0.0f;

	// Zero out the destination buffer
	std::memset(m_freqData, 0, sizeof(kiss_fft_cpx) * (m_blockSize / 2 + 1));

	// Calculate band width
	int band_width = m_band.m_endBin - m_band.m_startBin + 1;

	// Copy and shift the lowest band (0 to band_width-1) to target position
	for (int i = 0; i < band_width; i++) {
		int target_bin = m_band.m_startBin + i;
		if (target_bin < m_blockSize / 2 + 1) {
			m_freqData[target_bin] = fft_data[i];
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
	kiss_fftri(m_inverseCfg, m_freqData, m_timeData);

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

void FFTSyncCheck::BandProcessor::processBlock(const float *input, float *output)
{
	// Apply window and copy input to FFT buffer
	for (int i = 0; i < m_blockSize; i++) {
		m_timeData[i] = input[i] * m_window[i];
	}

	// Execute forward FFT
	kiss_fftr(m_forwardCfg, m_timeData, m_freqData);

	// Process the FFT data
	processBlockFFT(m_freqData, output);
}

void FFTSyncCheck::BandProcessor::processBlockWithShift(const float *input, float *output)
{
	// Apply window and copy input to FFT buffer
	for (int i = 0; i < m_blockSize; i++) {
		m_timeData[i] = input[i] * m_window[i];
	}

	// Execute forward FFT
	kiss_fftr(m_forwardCfg, m_timeData, m_freqData);

	// Process the FFT data with shift
	processBlockWithShiftFFT(m_freqData, output);
}

float FFTSyncCheck::BandProcessor::getBandIsolation() const
{
	return 10.0f * log10f(m_inBandPower / (m_outBandPower + 1e-10f));
}

void FFTSyncCheck::BandProcessor::allocateResources()
{
	m_timeData = new float[m_blockSize];
	m_freqData = new kiss_fft_cpx[m_blockSize / 2 + 1];
	m_forwardCfg = kiss_fftr_alloc(m_blockSize, 0, nullptr, nullptr);
	m_inverseCfg = kiss_fftr_alloc(m_blockSize, 1, nullptr, nullptr);
}

void FFTSyncCheck::BandProcessor::freeResources()
{
	free(m_forwardCfg);
	free(m_inverseCfg);
	delete[] m_timeData;
	delete[] m_freqData;
}

void FFTSyncCheck::BandProcessor::applyWindow(const float* input, float* output, int size) const
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
	, m_analysisForwardCfg(nullptr)
	, m_analysisInverseCfg(nullptr)
	, m_analysisTimeData(nullptr)
	, m_analysisFreqData(nullptr)
	, m_correlationData(nullptr)
	, m_paddedForwardCfg(nullptr)
	, m_paddedInverseCfg(nullptr)
{
	// Allocate buffers for analysis.
	m_analysisTimeData = new float[m_fftSize];
	m_analysisFreqData = new kiss_fft_cpx[m_fftSize / 2 + 1];
	m_correlationData = new kiss_fft_cpx[m_fftSize / 2 + 1];

	// Create Kiss FFT configurations.
	m_analysisForwardCfg = kiss_fftr_alloc(m_fftSize, 0, nullptr, nullptr);
	m_analysisInverseCfg = kiss_fftr_alloc(m_fftSize, 1, nullptr, nullptr);

	// Create padded FFT configurations for delay calculations
	const int INTERP_FACTOR = 4;
	const int padded_size = m_fftSize * INTERP_FACTOR;
	m_paddedForwardCfg = kiss_fftr_alloc(padded_size, 0, nullptr, nullptr);
	m_paddedInverseCfg = kiss_fftr_alloc(padded_size, 1, nullptr, nullptr);
	m_paddedTimeData.resize(padded_size);
	m_paddedFreqData.resize(padded_size / 2 + 1);
	m_paddedCorrData.resize(padded_size / 2 + 1);

	// Calculate band boundaries.
	int bins_per_band = (m_fftSize / 2) / NUM_BANDS;
	for (int i = 0; i < NUM_BANDS; i++) {
		m_bands[i].m_startBin = i * bins_per_band;
		m_bands[i].m_endBin = (i + 1) * bins_per_band - 1;
	}
}

FFTSyncCheck::~FFTSyncCheck()
{
	free(m_analysisForwardCfg);
	free(m_analysisInverseCfg);
	free(m_paddedForwardCfg);
	free(m_paddedInverseCfg);
	delete[] m_analysisTimeData;
	delete[] m_analysisFreqData;
	delete[] m_correlationData;
}

void FFTSyncCheck::extractBand(const kiss_fft_cpx *input, kiss_fft_cpx *output, int band_idx)
{
	std::memset(output, 0, sizeof(kiss_fft_cpx) * (m_fftSize / 2 + 1));
	if (band_idx >= 0 && band_idx < NUM_BANDS) {
		int start = m_bands[band_idx].m_startBin;
		int end = m_bands[band_idx].m_endBin;
		for (int i = start; i <= end; i++) {
			output[i].r = input[i].r;
			output[i].i = input[i].i;
		}
	}
}

float FFTSyncCheck::calculateBandDelayFFT(const kiss_fft_cpx* fft_data, int band1, int band2)
{
	const int INTERP_FACTOR = 4;
	const int padded_size = m_fftSize * INTERP_FACTOR;

	// Calculate band shift
	int band_shift = m_bands[band2].m_startBin - m_bands[band1].m_startBin;
	int band_width = m_bands[band1].m_endBin - m_bands[band1].m_startBin + 1;

	// Scale band indices for padded size
	band_shift *= INTERP_FACTOR;
	band_width *= INTERP_FACTOR;

	// Cross-correlation in frequency domain
	std::memset(m_paddedCorrData.data(), 0, sizeof(kiss_fft_cpx) * (padded_size / 2 + 1));

	// Correlate corresponding bins from both bands
	for (int i = 0; i < band_width; i++) {
		int bin1 = m_bands[band1].m_startBin * INTERP_FACTOR + i;
		int bin2 = bin1 + band_shift;

		if (bin2 <= m_bands[band2].m_endBin * INTERP_FACTOR) {
			float real1 = m_paddedFreqData[bin1].r;
			float imag1 = m_paddedFreqData[bin1].i;
			float real2 = m_paddedFreqData[bin2].r;
			float imag2 = m_paddedFreqData[bin2].i;

			// Normalize magnitudes
			float mag1 = sqrtf(real1 * real1 + imag1 * imag1);
			float mag2 = sqrtf(real2 * real2 + imag2 * imag2);

			if (mag1 > 1e-10f && mag2 > 1e-10f) {
				real1 /= mag1;
				imag1 /= mag1;
				real2 /= mag2;
				imag2 /= mag2;

				m_paddedCorrData[i].r = real2 * real1 + imag2 * imag1;
				m_paddedCorrData[i].i = imag2 * real1 - real2 * imag1;
			}
		}
	}

	// Execute inverse FFT using pre-allocated configuration
	kiss_fftri(m_paddedInverseCfg, m_paddedCorrData.data(), m_paddedTimeData.data());

	// Find maximum correlation peak and neighboring samples
	float max_corr = 0.0f;
	int max_idx = 0;
	for (int i = 0; i < padded_size; i++) {
		float abs_val = fabsf(m_paddedTimeData[i]);
		if (abs_val > max_corr) {
			max_corr = abs_val;
			max_idx = i;
		}
	}

	// Parabolic interpolation for sub-sample accuracy
	float alpha = m_paddedTimeData[(max_idx + padded_size - 1) % padded_size];
	float beta = m_paddedTimeData[max_idx];
	float gamma = m_paddedTimeData[(max_idx + 1) % padded_size];

	// p will be in range [-0.5, 0.5], indicating the fractional sample offset
	float p = 0.5f * (alpha - gamma) / (alpha - 2.0f * beta + gamma);
	float interpolated_idx = max_idx + p;

	// Convert index to time delay
	float delay = 0.0f;
	if (interpolated_idx > padded_size / 2) {
		delay = -(padded_size - interpolated_idx) / static_cast<float>(m_sampleRate);
	} else {
		delay = interpolated_idx / static_cast<float>(m_sampleRate);
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
	const int INTERP_FACTOR = 4;
	const int padded_size = m_fftSize * INTERP_FACTOR;

	// Apply Hann window to the time-domain signal and zero-pad
	std::memset(m_paddedTimeData.data(), 0, sizeof(float) * padded_size);
	for (int i = 0; i < m_fftSize; i++) {
		float window_val = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (m_fftSize - 1)));
		m_paddedTimeData[i] = samples[i] * window_val;
	}

	// Execute forward FFT once using pre-allocated configuration
	kiss_fftr(m_paddedForwardCfg, m_paddedTimeData.data(), m_paddedFreqData.data());

	// Calculate delays relative to band 0 using the pre-computed FFT data
	for (int i = 1; i < NUM_BANDS; i++) {
		float delay = calculateBandDelayFFT(m_paddedFreqData.data(), 0, i);
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

std::vector<float> FFTSyncCheck::generateTestSignal(float duration, const std::vector<float> &delays_ms, bool verbose)
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

		// Calculate band width and leave 10% guard band on each side
		int band_width = m_bands[0].m_endBin - m_bands[0].m_startBin + 1;
		int guard_bins = band_width / 10; // 10% guard band

		// Create a wider reference band
		Band ref_band = {
			m_bands[0].m_startBin + guard_bins, // Start after guard band
			m_bands[0].m_endBin - guard_bins	// End before guard band
		};

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

	// Temporary buffers for FFT processing
	std::vector<float> windowed_input(BLOCK_SIZE);
	std::vector<kiss_fft_cpx> fft_data(BLOCK_SIZE / 2 + 1);
	kiss_fftr_cfg fft_cfg = kiss_fftr_alloc(BLOCK_SIZE, 0, nullptr, nullptr);

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
		std::memcpy(input_block.data(), reference_signal.data() + offset, samples_to_copy * sizeof(float));

		// Apply window and perform FFT once
		processors[0].applyWindow(input_block.data(), windowed_input.data(), BLOCK_SIZE);
		kiss_fftr(fft_cfg, windowed_input.data(), fft_data.data());

		// Process each band using the same FFT data
		for (int band = 0; band < NUM_BANDS; band++) {
			processors[band].processBlockWithShiftFFT(fft_data.data(), block_output.data());

			// Store band signal and add to mixed output with appropriate delay
			int valid_samples = std::min(HOP_SIZE, num_samples - offset);
			int delay_samples = static_cast<int>(delays_ms[band] * m_sampleRate / 1000);

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

	// Clean up
	free(fft_cfg);

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