#include "BandProcessor.h"
#include <assert.h>

BandProcessor::BandProcessor(const SyncCheckConfig &config, const SyncCheckConfig::Band &band)
	: m_config(config)
	, m_forwardCfg(kiss_fftr_alloc(config.getBlockSize(), 0, nullptr, nullptr), free)
	, m_inverseCfg(kiss_fftr_alloc(config.getBlockSize(), 1, nullptr, nullptr), free)
	, m_timeData(config.getBlockSize())
	, m_freqData(config.getBlockSize() / 2 + 1)
	, m_workingFreqData(config.getBlockSize() / 2 + 1)
	, m_band(band)
	, m_overlapBuffer(config.getOverlapSize(), 0.0f)
	, m_window(config.getBlockSize(), 0.0f)
	, m_inBandPower(0.0)
	, m_outBandPower(0.0)
{
#ifdef USE_BARTLETT_WINDOW
	// Bartlett window - triangular window normalized for hop size
	const float hop_factor = m_config.getBlockSize() / config.getHopSize();
	for (int i = 0; i < m_config.getBlockSize(); i++) {
		float bartlett = 1.0f - fabsf(2.0f * i - (m_config.getBlockSize() - 1)) / (m_config.getBlockSize() - 1);
		m_window[i] = bartlett / sqrtf(hop_factor);  // Normalize for perfect reconstruction with hop size
	}
#else
	// Square root of Hann window - perfect for 50% overlap when applied twice
	const float hop_factor = m_config.getBlockSize() / config.getHopSize();
	for (std::size_t i = 0; i < m_config.getBlockSize(); i++) {
		float hann = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (m_config.getBlockSize() - 1)));
		m_window[i] = sqrtf(hann) /  sqrtf(hop_factor);  // Square root for analysis/synthesis
	}
#endif
}

BandProcessor::BandProcessor(const BandProcessor &other)
	: m_config(other.m_config)
	, m_forwardCfg(kiss_fftr_alloc(other.m_config.getBlockSize(), 0, nullptr, nullptr), free)
	, m_inverseCfg(kiss_fftr_alloc(other.m_config.getBlockSize(), 1, nullptr, nullptr), free)
	, m_timeData(other.m_timeData)
	, m_freqData(other.m_freqData)
	, m_workingFreqData(other.m_workingFreqData)
	, m_band(other.m_band)
	, m_overlapBuffer(other.m_overlapBuffer)
	, m_window(other.m_window)
	, m_inBandPower(other.m_inBandPower)
	, m_outBandPower(other.m_outBandPower)
{
}

BandProcessor::BandProcessor(BandProcessor &&other) noexcept
	: m_config(other.m_config)
	, m_forwardCfg(std::move(other.m_forwardCfg))
	, m_inverseCfg(std::move(other.m_inverseCfg))
	, m_timeData(std::move(other.m_timeData))
	, m_freqData(std::move(other.m_freqData))
	, m_workingFreqData(std::move(other.m_workingFreqData))
	, m_band(other.m_band)
	, m_overlapBuffer(std::move(other.m_overlapBuffer))
	, m_window(std::move(other.m_window))
	, m_inBandPower(other.m_inBandPower)
	, m_outBandPower(other.m_outBandPower)
{
}

BandProcessor::~BandProcessor() = default;

BandProcessor &BandProcessor::operator=(const BandProcessor &other)
{
	if (this != &other) {
		m_config = other.m_config;
		m_forwardCfg.reset(kiss_fftr_alloc(other.m_config.getBlockSize(), 0, nullptr, nullptr));
		m_inverseCfg.reset(kiss_fftr_alloc(other.m_config.getBlockSize(), 1, nullptr, nullptr));
		m_timeData = other.m_timeData;
		m_freqData = other.m_freqData;
		m_workingFreqData = other.m_workingFreqData;
		m_band = other.m_band;
		m_overlapBuffer = other.m_overlapBuffer;
		m_window = other.m_window;
		m_inBandPower = other.m_inBandPower;
		m_outBandPower = other.m_outBandPower;
	}
	return *this;
}

BandProcessor &BandProcessor::operator=(BandProcessor &&other) noexcept
{
	if (this != &other) {
		m_config = other.m_config;
		m_forwardCfg = std::move(other.m_forwardCfg);
		m_inverseCfg = std::move(other.m_inverseCfg);
		m_timeData = std::move(other.m_timeData);
		m_freqData = std::move(other.m_freqData);
		m_workingFreqData = std::move(other.m_workingFreqData);
		m_band = other.m_band;
		m_overlapBuffer = std::move(other.m_overlapBuffer);
		m_window = std::move(other.m_window);
		m_inBandPower = other.m_inBandPower;
		m_outBandPower = other.m_outBandPower;
	}
	return *this;
}

void BandProcessor::processBlockFFT(const std::vector<kiss_fft_cpx>& fft_data, std::vector<float>& output)
{
	assert(output.size() == m_config.getBlockSize());

	// Copy input FFT data
	std::copy(fft_data.begin(), fft_data.begin() + m_config.getBlockSize() / 2 + 1, m_workingFreqData.begin());

	// Band-pass filter and measure power
	m_inBandPower = 0.0f;
	m_outBandPower = 0.0f;
	for (std::size_t i = 0; i < m_config.getBlockSize() / 2 + 1; i++) {
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
	for (std::size_t i = 0; i < m_config.getBlockSize(); i++) {
		m_timeData[i] = m_timeData[i] * m_window[i] / m_config.getBlockSize();
	}

	// Overlap-add: First part combines with previous overlap
	for (std::size_t i = 0; i < m_config.getOverlapSize(); i++) {
		output[i] = m_overlapBuffer[i] + m_timeData[i];
	}

	// Second part is just the current block
	for (std::size_t i = m_config.getOverlapSize(); i < m_config.getBlockSize(); i++) {
		output[i] = m_timeData[i];
	}

	// Save the overlap for next time
	for (std::size_t i = 0; i < m_config.getOverlapSize(); i++) {
		m_overlapBuffer[i] = m_timeData[m_config.getBlockSize() - m_config.getOverlapSize() + i];
	}
}

void BandProcessor::processBlockWithShiftFFT(const std::vector<kiss_fft_cpx>& fft_data, std::vector<float>& output,
										   const std::size_t clearStartBin, const std::size_t clearEndBin)
{
	assert(output.size() == m_config.getBlockSize());

	// Reset power measurements
	m_inBandPower = 0.0f;
	m_outBandPower = 0.0f;

	// Copy all input FFT data first to preserve all other frequency bins
	std::copy(fft_data.begin(), fft_data.begin() + m_config.getBlockSize() / 2 + 1, m_workingFreqData.begin());

	// Clear the entire frequency range that could be affected by any band signal
	for (std::size_t i = clearStartBin; i <= clearEndBin && i < m_config.getBlockSize() / 2 + 1; i++) {
		m_workingFreqData[i] = kiss_fft_cpx{0.0f, 0.0f};
	}

	// Calculate band width
	std::size_t band_width = m_band.m_endBin - m_band.m_startBin + 1;

	// Copy the source bins (0 to band_width-1) to target position
	for (std::size_t i = 0; i < band_width; i++) {
		std::size_t target_bin = m_band.m_startBin + i;
		if (target_bin < m_config.getBlockSize() / 2 + 1) {
			m_workingFreqData[target_bin] = fft_data[i];
			float power = fft_data[i].r * fft_data[i].r + fft_data[i].i * fft_data[i].i;
			m_inBandPower += power;
		}
	}

	// Calculate power for bins outside our band
	for (std::size_t i = 0; i < m_config.getBlockSize() / 2 + 1; i++) {
		if (i < m_band.m_startBin || i > m_band.m_endBin) {
			float power = m_workingFreqData[i].r * m_workingFreqData[i].r +
						 m_workingFreqData[i].i * m_workingFreqData[i].i;
			m_outBandPower += power;
		}
	}

	// Execute inverse FFT
	kiss_fftri(m_inverseCfg.get(), m_workingFreqData.data(), m_timeData.data());

	// Apply scaling and window
	for (std::size_t i = 0; i < m_config.getBlockSize(); i++) {
		m_timeData[i] = m_timeData[i] * m_window[i] / m_config.getBlockSize();
	}

	// Overlap-add: First part combines with previous overlap
	for (std::size_t i = 0; i < m_config.getOverlapSize(); i++) {
		output[i] = m_overlapBuffer[i] + m_timeData[i];
	}

	// Second part is just the current block
	for (std::size_t i = m_config.getOverlapSize(); i < m_config.getBlockSize(); i++) {
		output[i] = m_timeData[i];
	}

	// Save the overlap for next time
	for (std::size_t i = 0; i < m_config.getOverlapSize(); i++) {
		m_overlapBuffer[i] = m_timeData[m_config.getBlockSize() - m_config.getOverlapSize() + i];
	}
}

const std::vector<kiss_fft_cpx>& BandProcessor::applyWindowAndFFT(const std::vector<float>& input)
{
	assert(input.size() == m_config.getBlockSize());

	// Apply window and copy input to FFT buffer
	for (std::size_t i = 0; i < m_config.getBlockSize(); i++) {
		m_timeData[i] = input[i] * m_window[i];
	}

	// Execute forward FFT
	kiss_fftr(m_forwardCfg.get(), m_timeData.data(), m_freqData.data());
	return m_freqData;
}

void BandProcessor::processBlock(const std::vector<float>& input, std::vector<float>& output)
{
	assert(input.size() == m_config.getBlockSize());
	assert(output.size() == m_config.getBlockSize());

	// Apply window and copy input to FFT buffer
	applyWindowAndFFT(input);

	// Process the FFT data
	processBlockFFT(m_freqData, output);
}

void BandProcessor::processBlockWithShift(const std::vector<float>& input, std::vector<float>& output, const std::size_t clearStartBin, const std::size_t clearEndBin)
{
	assert(input.size() == m_config.getBlockSize());
	assert(output.size() == m_config.getBlockSize());

	// Apply window and copy input to FFT buffer
	applyWindowAndFFT(input);

	// Process the FFT data with shift
	// Only clear the range from 0 to the band width since that's where the source data comes from
	processBlockWithShiftFFT(m_freqData, output, clearStartBin, clearEndBin);
}

float BandProcessor::getBandIsolation() const
{
	return 10.0f * log10f(m_inBandPower / (m_outBandPower + 1e-10f));
}
