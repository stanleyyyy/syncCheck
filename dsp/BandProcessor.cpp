#include "BandProcessor.h"
#include "Logger.h"

BandProcessor::BandProcessor(const SyncCheckConfig &config, const SyncCheckConfig::Band &band)
	: m_config(config)
	, m_stftProcessor(std::make_unique<STFTProcessor>(config.getBlockSize(), config.getHopSize()))
	, m_band(band)
	, m_workingFreqData(config.getBlockSize() / 2 + 1)
	, m_inBandPower(0.0)
	, m_outBandPower(0.0)
{
	// Determine an optimal taper fraction.
	// For example, if a desired transition width is 500 Hz then
	// taperFraction = desiredTransitionWidth / bandWidth, limited to 0.5 at most.
	float desiredTransitionWidth = 500.0f; // Hz
	float bandWidthHz = m_band.getEndFreq() - m_band.getStartFreq();
	m_shiftTaperFraction = computeTaperFraction(desiredTransitionWidth, bandWidthHz);

	// Precompute the taper window for the shifting band defined by m_band.
	std::size_t bandStartBin = m_band.getStartBin(m_config.getBlockSize(), m_config.getSampleRate());
	std::size_t bandEndBin = m_band.getEndBin(m_config.getBlockSize(), m_config.getSampleRate());
	std::size_t shiftBinCount = (bandEndBin >= bandStartBin) ? (bandEndBin - bandStartBin + 1) : 0;
	m_shiftTaper = computeTaperWindow(shiftBinCount, m_shiftTaperFraction);
}

BandProcessor::BandProcessor(const BandProcessor &other)
	: m_config(other.m_config)
	, m_stftProcessor(std::make_unique<STFTProcessor>(other.m_config.getBlockSize(), other.m_config.getHopSize()))
	, m_band(other.m_band)
	, m_workingFreqData(other.m_workingFreqData)
	, m_inBandPower(other.m_inBandPower)
	, m_outBandPower(other.m_outBandPower)
	, m_shiftTaper(other.m_shiftTaper)
	, m_clearTaper(other.m_clearTaper)
	, m_shiftTaperFraction(other.m_shiftTaperFraction)
	, m_clearTaperFraction(other.m_clearTaperFraction)
{
}

BandProcessor::BandProcessor(BandProcessor &&other) noexcept
	: m_config(other.m_config)
	, m_stftProcessor(std::move(other.m_stftProcessor))
	, m_band(other.m_band)
	, m_workingFreqData(std::move(other.m_workingFreqData))
	, m_inBandPower(other.m_inBandPower)
	, m_outBandPower(other.m_outBandPower)
	, m_shiftTaper(std::move(other.m_shiftTaper))
	, m_clearTaper(std::move(other.m_clearTaper))
	, m_shiftTaperFraction(other.m_shiftTaperFraction)
	, m_clearTaperFraction(other.m_clearTaperFraction)
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
		m_shiftTaper = other.m_shiftTaper;
		m_clearTaper = other.m_clearTaper;
		m_shiftTaperFraction = other.m_shiftTaperFraction;
		m_clearTaperFraction = other.m_clearTaperFraction;
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
		m_shiftTaper = std::move(other.m_shiftTaper);
		m_clearTaper = std::move(other.m_clearTaper);
		m_shiftTaperFraction = other.m_shiftTaperFraction;
		m_clearTaperFraction = other.m_clearTaperFraction;
	}
	return *this;
}

void BandProcessor::processBlock(const std::vector<float>& input, std::vector<float>& output)
{
	ASSERT_WITH_ERROR(input.size() == m_config.getBlockSize(), "Input block size must match the configured block size.");
	ASSERT_WITH_ERROR(output.size() == m_config.getBlockSize(), "Output block size must match the configured block size.");

	// Perform STFT analysis
	auto frames = m_stftProcessor->forward(input);

	if (frames.empty()) {
		return;
	}

	// Get frequency domain data
	auto& fft_data = frames[0];
	processBlockFFT(fft_data, output);
}

void BandProcessor::processBlockFFT(const std::vector<kiss_fft_cpx> &fft_data, std::vector<float>& output)
{
	output.resize(m_config.getBlockSize());
	ASSERT_WITH_ERROR(output.size() == m_config.getBlockSize(), "Output block size must match the configured block size.");

	std::size_t bandStartBin = m_band.getStartBin(m_config.getBlockSize(), m_config.getSampleRate());
	std::size_t bandEndBin = m_band.getEndBin(m_config.getBlockSize(), m_config.getSampleRate());

	std::copy(fft_data.begin(), fft_data.end(), m_workingFreqData.begin());

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
			m_workingFreqData[i].r = 0.0f;
			m_workingFreqData[i].i = 0.0f;
		}
	}

	// Create frames for inverse STFT
	std::vector<std::vector<kiss_fft_cpx>> frames;
	frames.push_back(m_workingFreqData);

	// Perform inverse STFT
	m_stftProcessor->inverse(frames, output);
}

void BandProcessor::processBlockWithShift(const std::vector<float>& input_background,
										const std::vector<float>& input_to_shift,
										std::vector<float>& output,
										const SyncCheckConfig::Band &clearBand)
{
	ASSERT_WITH_ERROR(input_background.size() == m_config.getBlockSize(), "Input background block size must match the configured block size.");
	ASSERT_WITH_ERROR(input_to_shift.size() == m_config.getBlockSize(), "Input to shift block size must match the configured block size.");
	ASSERT_WITH_ERROR(output.size() == m_config.getBlockSize(), "Output block size must match the configured block size.");

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

void BandProcessor::forward(const std::vector<float>& input, std::vector<kiss_fft_cpx>& output)
{
	// Perform STFT analysis
	auto frames = m_stftProcessor->forward(input);

	if (frames.empty()) {
		return;
	}

	// Copy the FFT data to the output
	output.resize(frames[0].size());
	std::copy(frames[0].begin(), frames[0].end(), output.begin());
}

void BandProcessor::processBlockWithShiftFFT(const std::vector<kiss_fft_cpx> &fft_data_background, const std::vector<kiss_fft_cpx> &fft_data_to_shift,
											 std::vector<float> &output, const SyncCheckConfig::Band &clearBand)
{
	output.resize(m_config.getBlockSize());
	ASSERT_WITH_ERROR(output.size() == m_config.getBlockSize(), "Output block size must match the configured block size.");

	std::size_t clearStartBin = clearBand.getStartBin(m_config.getBlockSize(), m_config.getSampleRate());
	std::size_t clearEndBin = clearBand.getEndBin(m_config.getBlockSize(), m_config.getSampleRate());
	std::size_t bandStartBin = m_band.getStartBin(m_config.getBlockSize(), m_config.getSampleRate());
	std::size_t bandEndBin = m_band.getEndBin(m_config.getBlockSize(), m_config.getSampleRate());

	m_inBandPower = 0.0f;
	m_outBandPower = 0.0f;

	// Copy background into working frequency data. Do not copy anything after the clear band end bin.
	std::copy(fft_data_background.begin(), fft_data_background.begin() + clearEndBin, m_workingFreqData.begin());

	const double sampleRate = static_cast<double>(m_config.getSampleRate());
	const double blockSize = static_cast<double>(m_config.getBlockSize());

	// Recompute the taper window for the clear band if it has changed.
	std::size_t clearBinCount = clearEndBin - clearStartBin + 1;
	if (clearBinCount != m_clearTaper.size()) {
		float desiredTransitionWidth = 500.0f; // Hz
		float bandWidthHz = clearBand.getEndFreq() - clearBand.getStartFreq();
		m_clearTaperFraction = computeTaperFraction(desiredTransitionWidth, bandWidthHz);
		m_clearTaper = computeTaperWindow(clearBinCount, m_clearTaperFraction, true);
	}

	// Start clearing earlier so that when the taper ends, we will get fully cleared band
	const std::size_t clearTaperLength = m_clearTaperFraction * (clearEndBin - clearStartBin + 1);
	if (clearStartBin > clearTaperLength)
		clearStartBin -= clearTaperLength;
	else
		clearStartBin = 0;

	for (std::size_t i = clearStartBin, j = 0; i <= clearEndBin && i < m_workingFreqData.size(); i++, j++) {
		float taper = m_clearTaper[j];
		// Multiply by (1 - taper) so that the band is smoothly cleared.
		m_workingFreqData[i].r *= (1.0f - taper);
		m_workingFreqData[i].i *= (1.0f - taper);
	}

	// --- Frequency shift with smooth cosine rolloff using the precomputed taper.
	for (std::size_t i = 0, shiftBins = m_shiftTaper.size(); i < shiftBins && i < fft_data_to_shift.size(); i++) {
		std::size_t srcBin = i;
		std::size_t dstBin = bandStartBin + i;
		if (dstBin >= m_workingFreqData.size()) {
			continue;
		}

		// Phase calculation based on source frequency.
		double srcBinFrequency = (static_cast<double>(srcBin) * sampleRate) / blockSize;
		double phaseOffset = srcBinFrequency * (2.0 * M_PI / sampleRate) * m_config.getBlockSize();

		double cosPhase = std::cos(phaseOffset);
		double sinPhase = std::sin(phaseOffset);

		float re = fft_data_to_shift[srcBin].r;
		float im = fft_data_to_shift[srcBin].i;
		float taper = m_shiftTaper[i];

		// Apply phase shift and taper.
		m_workingFreqData[dstBin].r = static_cast<float>((re * cosPhase - im * sinPhase) * taper);
		m_workingFreqData[dstBin].i = static_cast<float>((re * sinPhase + im * cosPhase) * taper);

		m_inBandPower += (re * re + im * im) * taper * taper;
	}

	// Accumulate power outside the band.
	for (std::size_t i = 0; i < m_workingFreqData.size(); i++) {
		if (i < bandStartBin || i > bandEndBin) {
			float re = m_workingFreqData[i].r;
			float im = m_workingFreqData[i].i;
			m_outBandPower += (re * re + im * im);
		}
	}

	// Create frames for inverse STFT.
	std::vector<std::vector<kiss_fft_cpx>> frames;
	frames.push_back(m_workingFreqData);

	// Perform inverse STFT.
	m_stftProcessor->inverse(frames, output);
}

void BandProcessor::inverse(const std::vector<std::vector<kiss_fft_cpx>> &frames, std::vector<float> &output)
{
	m_stftProcessor->inverse(frames, output);
}

float BandProcessor::computeTaperFraction(float desiredTransitionWidth, float bandWidthHz)
{
	return (bandWidthHz > 0.0f) ? std::min(0.5f, desiredTransitionWidth / bandWidthHz) : 0.1f;
}

std::vector<float> BandProcessor::computeTaperWindow(std::size_t length, float taperFraction, const bool &isClearBand)
{
	std::vector<float> window(length, 1.0f);
	if (length < 2) {
		return window;
	}
	for (std::size_t i = 0; i < length; i++) {
		float relativePos = static_cast<float>(i) / static_cast<float>(length - 1);
		if (relativePos < taperFraction) {
			window[i] = 0.5f * (1.0f - std::cos(M_PI * (relativePos / taperFraction)));
		}
		// If the band is not the clear band, apply a taper to the end of the band.
		else if ((relativePos > (1.0f - taperFraction)) && !isClearBand) {
			window[i] = 0.5f * (1.0f - std::cos(M_PI * ((1.0f - relativePos) / taperFraction)));
		}
		else {
			window[i] = 1.0f;
		}
	}
	return window;
}
