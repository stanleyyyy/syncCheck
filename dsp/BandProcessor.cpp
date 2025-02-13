#include "BandProcessor.h"
#include <assert.h>

BandProcessor::BandProcessor(const SyncCheckConfig &config, const SyncCheckConfig::Band &band)
	: m_config(config)
	, m_stftProcessor(std::make_unique<STFTProcessor>(config.getBlockSize(), config.getHopSize()))
	, m_band(band)
	, m_workingFreqData(config.getBlockSize() / 2 + 1)
	, m_inBandPower(0.0)
	, m_outBandPower(0.0)
{
}

BandProcessor::BandProcessor(const BandProcessor &other)
	: m_config(other.m_config)
	, m_stftProcessor(std::make_unique<STFTProcessor>(other.m_config.getBlockSize(), other.m_config.getHopSize()))
	, m_band(other.m_band)
	, m_workingFreqData(other.m_workingFreqData)
	, m_inBandPower(other.m_inBandPower)
	, m_outBandPower(other.m_outBandPower)
{
}

BandProcessor::BandProcessor(BandProcessor &&other) noexcept
	: m_config(other.m_config)
	, m_stftProcessor(std::move(other.m_stftProcessor))
	, m_band(other.m_band)
	, m_workingFreqData(std::move(other.m_workingFreqData))
	, m_inBandPower(other.m_inBandPower)
	, m_outBandPower(other.m_outBandPower)
{
}

BandProcessor::~BandProcessor() = default;

BandProcessor &BandProcessor::operator=(const BandProcessor &other)
{
	if (this != &other) {
		m_config = other.m_config;
		m_stftProcessor = std::make_unique<STFTProcessor>(other.m_config.getBlockSize(), other.m_config.getHopSize());
		m_band = other.m_band;
		m_workingFreqData = other.m_workingFreqData;
		m_inBandPower = other.m_inBandPower;
		m_outBandPower = other.m_outBandPower;
	}
	return *this;
}

BandProcessor &BandProcessor::operator=(BandProcessor &&other) noexcept
{
	if (this != &other) {
		m_config = other.m_config;
		m_stftProcessor = std::move(other.m_stftProcessor);
		m_band = other.m_band;
		m_workingFreqData = std::move(other.m_workingFreqData);
		m_inBandPower = other.m_inBandPower;
		m_outBandPower = other.m_outBandPower;
	}
	return *this;
}

void BandProcessor::processBlock(const std::vector<float>& input, std::vector<float>& output)
{
	assert(input.size() == m_config.getBlockSize());
	assert(output.size() == m_config.getBlockSize());

	// Perform STFT analysis
	auto frames = m_stftProcessor->forward(input);

	if (frames.empty()) {
		return;
	}

	// Get frequency domain data
	auto& fft_data = frames[0];

	std::size_t bandStartBin = m_band.getStartBin(m_config.getBlockSize(), m_config.getSampleRate());
	std::size_t bandEndBin = m_band.getEndBin(m_config.getBlockSize(), m_config.getSampleRate());

	// Band-pass filter and measure power
	m_inBandPower = 0.0f;
	m_outBandPower = 0.0f;
	for (std::size_t i = 0; i < fft_data.size(); i++) {
		float real_val = fft_data[i].r;
		float imag_val = fft_data[i].i;
		float power = real_val * real_val + imag_val * imag_val;
		if (i >= bandStartBin && i <= bandEndBin) {
			m_inBandPower += power;
		}
		else {
			m_outBandPower += power;
			fft_data[i].r = 0.0f;
			fft_data[i].i = 0.0f;
		}
	}

	// Perform inverse STFT
	output = m_stftProcessor->inverse(frames);
}

void BandProcessor::processBlockWithShift(const std::vector<float>& input_background,
										const std::vector<float>& input_to_shift,
										std::vector<float>& output,
										const SyncCheckConfig::Band &clearBand)
{
	assert(input_background.size() == m_config.getBlockSize());
	assert(input_to_shift.size() == m_config.getBlockSize());
	assert(output.size() == m_config.getBlockSize());

	// Perform STFT analysis on both signals
	auto frames_background = m_stftProcessor->forward(input_background);
	auto frames_to_shift = m_stftProcessor->forward(input_to_shift);

	if (frames_background.empty() || frames_to_shift.empty()) {
		return;
	}

	processBlockWithShiftFFT(frames_background[0], frames_to_shift[0], output, clearBand);
}

float BandProcessor::getBandIsolation() const
{
	return 10.0f * log10f(m_inBandPower / (m_outBandPower + 1e-10f));
}

void BandProcessor::applyWindowAndFFT(const std::vector<float>& input, std::vector<kiss_fft_cpx>& output)
{
	// Perform STFT analysis
	auto frames = m_stftProcessor->forward(input);

	if (frames.empty()) {
		return;
	}

	// Copy the FFT data to the output
	output.resize(m_config.getBlockSize() / 2 + 1);
	std::copy(frames[0].begin(), frames[0].end(), output.begin());
}

void BandProcessor::processBlockWithShiftFFT(
		const std::vector<kiss_fft_cpx>& fft_data_background,
		const std::vector<kiss_fft_cpx>& fft_data_to_shift,
		std::vector<float>& output,
		const SyncCheckConfig::Band& clearBand)
{
	output.resize(m_config.getBlockSize());
	assert(output.size() == m_config.getBlockSize());

	std::size_t clearStartBin = clearBand.getStartBin(m_config.getBlockSize(), m_config.getSampleRate());
	std::size_t clearEndBin   = clearBand.getEndBin(m_config.getBlockSize(), m_config.getSampleRate());
	std::size_t bandStartBin  = m_band.getStartBin(m_config.getBlockSize(), m_config.getSampleRate());
	std::size_t bandEndBin    = m_band.getEndBin(m_config.getBlockSize(), m_config.getSampleRate());

	m_inBandPower  = 0.0f;
	m_outBandPower = 0.0f;

	// Copy background into workingFreqData, then clear out the target band.
	std::copy(fft_data_background.begin(), fft_data_background.end(), m_workingFreqData.begin());
	for (std::size_t i = clearStartBin; i <= clearEndBin && i < m_workingFreqData.size(); i++) {
		m_workingFreqData[i] = kiss_fft_cpx{0.0f, 0.0f};
	}

	// Compute the exact frequency shift in radians/sample
	double sampleRate       = static_cast<double>(m_config.getSampleRate());
	double blockSize        = static_cast<double>(m_config.getBlockSize());

	// **Correct Phase Calculation: Use Frequency-Dependent Phase Offsets**
	for (std::size_t i = 0; i <= bandEndBin - bandStartBin && i < fft_data_to_shift.size(); i++) {
		std::size_t srcBin = i; // Source band starts at 0 Hz
		std::size_t dstBin = bandStartBin + i; // Shifted band starts at m_band.getStartFreq()

		// Ensure the shifted bin stays within the FFT size limits
		if (dstBin >= m_workingFreqData.size()) {
			continue;
		}

		// Compute phase offset for this bin based on its frequency
		double binFrequency = (static_cast<double>(srcBin) * sampleRate) / blockSize;
		double phaseOffset  = binFrequency * (2.0 * M_PI / sampleRate) * m_config.getBlockSize();

		// Compute cosine/sine components for IQ modulation
		double cosPhase = cos(phaseOffset);
		double sinPhase = sin(phaseOffset);

		// Extract magnitude and phase from input FFT
		float re = fft_data_to_shift[srcBin].r;
		float im = fft_data_to_shift[srcBin].i;

		// Apply IQ modulation with correct phase offset
		m_workingFreqData[dstBin].r = re * cosPhase - im * sinPhase;
		m_workingFreqData[dstBin].i = re * sinPhase + im * cosPhase;

		// Accumulate power within the band
		m_inBandPower += (re * re + im * im);
	}

	// Accumulate power outside the band
	for (std::size_t i = 0; i < m_workingFreqData.size(); i++) {
		if (i < bandStartBin || i > bandEndBin) {
			float re = m_workingFreqData[i].r;
			float im = m_workingFreqData[i].i;
			m_outBandPower += (re*re + im*im);
		}
	}

	// Create frames for inverse STFT
	std::vector<std::vector<kiss_fft_cpx>> frames;
	frames.push_back(m_workingFreqData);

	// Perform inverse STFT
	auto result = m_stftProcessor->inverse(frames);
	std::copy(result.begin(), result.end(), output.begin());
}
