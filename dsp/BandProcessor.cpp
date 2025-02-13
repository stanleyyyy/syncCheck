#include "BandProcessor.h"
#include <assert.h>

BandProcessor::BandProcessor(const SyncCheckConfig &config, const SyncCheckConfig::Band &band)
	: m_config(config)
	, m_stftProcessor(std::make_unique<STFTProcessor>(config.getBlockSize(), config.getHopSize()))
	, m_band(band)
	, m_workingFreqData(config.getBlockSize() / 2 + 1)
	, m_inBandPower(0.0)
	, m_outBandPower(0.0)
	, m_prevPhase(config.getBlockSize() / 2 + 1, 0.0f)
	, m_accPhase(config.getBlockSize() / 2 + 1, 0.0f)
	, m_hasPrevFrame(false)
{
}

BandProcessor::BandProcessor(const BandProcessor &other)
	: m_config(other.m_config)
	, m_stftProcessor(std::make_unique<STFTProcessor>(other.m_config.getBlockSize(), other.m_config.getHopSize()))
	, m_band(other.m_band)
	, m_workingFreqData(other.m_workingFreqData)
	, m_inBandPower(other.m_inBandPower)
	, m_outBandPower(other.m_outBandPower)
	, m_prevPhase(other.m_prevPhase)
	, m_accPhase(other.m_accPhase)
	, m_hasPrevFrame(other.m_hasPrevFrame)
{
}

BandProcessor::BandProcessor(BandProcessor &&other) noexcept
	: m_config(other.m_config)
	, m_stftProcessor(std::move(other.m_stftProcessor))
	, m_band(other.m_band)
	, m_workingFreqData(std::move(other.m_workingFreqData))
	, m_inBandPower(other.m_inBandPower)
	, m_outBandPower(other.m_outBandPower)
	, m_prevPhase(std::move(other.m_prevPhase))
	, m_accPhase(std::move(other.m_accPhase))
	, m_hasPrevFrame(other.m_hasPrevFrame)
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
		m_prevPhase = other.m_prevPhase;
		m_accPhase = other.m_accPhase;
		m_hasPrevFrame = other.m_hasPrevFrame;
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
		m_prevPhase = std::move(other.m_prevPhase);
		m_accPhase = std::move(other.m_accPhase);
		m_hasPrevFrame = other.m_hasPrevFrame;
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

	// Precompute phase vocoder parameters to avoid redundant floating-point operations
	const float twoPi   = 2.0f * static_cast<float>(M_PI);
	const float N       = static_cast<float>(m_config.getBlockSize());
	const float hop     = static_cast<float>(m_config.getHopSize());
	const float binFreq = twoPi / N; // radians per bin

	// Compute exact bin shift based on real frequency
	double bandStartFreq = m_band.getStartFreq();
	double bandEndFreq   = m_band.getEndFreq();
	double desiredCenterFreq = 0.5 * (bandStartFreq + bandEndFreq);

	double sourceCenterBin  = 0.5 * (bandStartBin + bandEndBin);  // Integer bin approximation of the source center
	double sourceCenterFreq = sourceCenterBin * (static_cast<double>(m_config.getSampleRate()) / m_config.getBlockSize());
	double deltaF           = desiredCenterFreq - sourceCenterFreq;
	double binSpacingHz     = static_cast<double>(m_config.getSampleRate()) / m_config.getBlockSize();
	double alpha            = deltaF / binSpacingHz; // Fractional bin shift

	// **Use integer iteration to speed up processing**
	std::size_t bandWidth = bandEndBin - bandStartBin + 1;
	for (std::size_t i = 0; i < bandWidth; i++) {
		std::size_t srcBin = bandStartBin + i;
		std::size_t dstBin = static_cast<std::size_t>(srcBin + alpha);  // Apply integer bin shift

		// Ensure target bin is within valid range
		if (dstBin >= m_workingFreqData.size()) {
			continue;
		}

		// Extract magnitude and phase from input FFT
		float re = fft_data_to_shift[i].r;
		float im = fft_data_to_shift[i].i;
		float mag = std::sqrt(re * re + im * im);
		float inputPhase = std::atan2(im, re);

		// Initialize accumulated phase on first frame
		if (!m_hasPrevFrame) {
			m_accPhase[srcBin] = inputPhase;
			m_prevPhase[srcBin] = inputPhase;
		}

		// Compute phase difference and unwrap (fast version using integer math)
		float phaseDiff = inputPhase - m_prevPhase[srcBin];
		while (phaseDiff > static_cast<float>(M_PI)) phaseDiff -= twoPi;
		while (phaseDiff < -static_cast<float>(M_PI)) phaseDiff += twoPi;

		// Compute expected and deviation phase advance
		float omega = binFreq * static_cast<float>(srcBin);
		float expectedAdvance = omega * hop;
		float phaseDeviation = phaseDiff - expectedAdvance;

		// Accumulate new phase
		m_accPhase[srcBin] += expectedAdvance + phaseDeviation;

		// Apply frequency shift offset (alpha bins)
		float shiftOffset = static_cast<float>(alpha) * binFreq * hop;
		m_accPhase[srcBin] += shiftOffset;

		// Store updated phase & magnitude into target FFT bin
		float outPhase = m_accPhase[srcBin];
		m_workingFreqData[dstBin].r = mag * std::cos(outPhase);
		m_workingFreqData[dstBin].i = mag * std::sin(outPhase);

		// Save current input phase as previous
		m_prevPhase[srcBin] = inputPhase;

		// Update band power
		m_inBandPower += (mag * mag);
	}

	// Mark that we now have a “previous frame”
	m_hasPrevFrame = true;

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
