#include "BandProcessor.h"
#include "Logger.h"

BandProcessor::BandProcessor(const SyncCheckConfig &config, const SyncCheckConfig::Band &band)
	: m_config(config)
	, m_stftProcessor(std::make_unique<STFTProcessor>(config.getBlockSize(), config.getHopSize()))
	, m_hilbertShifterBackground(std::make_unique<HilbertShifter>(config.getBlockSize(), config.getHopSize(), config.getSampleRate()))
	, m_hilbertShifterReference(std::make_unique<HilbertShifter>(config.getBlockSize(), config.getHopSize(), config.getSampleRate()))
	, m_band(band)
	, m_workingFreqData(config.getBlockSize() / 2 + 1)
	, m_inBandPower(0.0)
	, m_outBandPower(0.0)
{
}

BandProcessor::BandProcessor(const BandProcessor &other)
	: m_config(other.m_config)
	, m_stftProcessor(std::make_unique<STFTProcessor>(other.m_config.getBlockSize(), other.m_config.getHopSize()))
	, m_hilbertShifterBackground(std::make_unique<HilbertShifter>(other.m_config.getBlockSize(), other.m_config.getHopSize(), other.m_config.getSampleRate()))
	, m_hilbertShifterReference(std::make_unique<HilbertShifter>(other.m_config.getBlockSize(), other.m_config.getHopSize(), other.m_config.getSampleRate()))
	, m_band(other.m_band)
	, m_workingFreqData(other.m_workingFreqData)
	, m_inBandPower(other.m_inBandPower)
	, m_outBandPower(other.m_outBandPower)
	, m_referenceTaper(other.m_referenceTaper)
	, m_backgroundTaper(other.m_backgroundTaper)
{
}

BandProcessor::BandProcessor(BandProcessor &&other) noexcept
	: m_config(other.m_config)
	, m_stftProcessor(std::move(other.m_stftProcessor))
	, m_hilbertShifterBackground(std::move(other.m_hilbertShifterBackground))
	, m_hilbertShifterReference(std::move(other.m_hilbertShifterReference))
	, m_band(other.m_band)
	, m_workingFreqData(std::move(other.m_workingFreqData))
	, m_inBandPower(other.m_inBandPower)
	, m_outBandPower(other.m_outBandPower)
	, m_referenceTaper(std::move(other.m_referenceTaper))
	, m_backgroundTaper(std::move(other.m_backgroundTaper))
{
}

BandProcessor::~BandProcessor() = default;

BandProcessor &BandProcessor::operator=(const BandProcessor &other)
{
	if (this != &other) {
		m_config = other.m_config;
		m_stftProcessor = std::make_unique<STFTProcessor>(other.m_config.getBlockSize(), other.m_config.getHopSize());
		m_hilbertShifterBackground = std::make_unique<HilbertShifter>(other.m_config.getBlockSize(), other.m_config.getHopSize(), other.m_config.getSampleRate());
		m_hilbertShifterReference = std::make_unique<HilbertShifter>(other.m_config.getBlockSize(), other.m_config.getHopSize(), other.m_config.getSampleRate());
		m_band = other.m_band;
		m_workingFreqData = other.m_workingFreqData;
		m_inBandPower = other.m_inBandPower;
		m_outBandPower = other.m_outBandPower;
		m_referenceTaper = other.m_referenceTaper;
		m_backgroundTaper = other.m_backgroundTaper;
	}
	return *this;
}

BandProcessor &BandProcessor::operator=(BandProcessor &&other) noexcept
{
	if (this != &other) {
		m_config = other.m_config;
		m_stftProcessor = std::move(other.m_stftProcessor);
		m_hilbertShifterBackground = std::move(other.m_hilbertShifterBackground);
		m_band = other.m_band;
		m_workingFreqData = std::move(other.m_workingFreqData);
		m_inBandPower = other.m_inBandPower;
		m_outBandPower = other.m_outBandPower;
		m_referenceTaper = std::move(other.m_referenceTaper);
		m_backgroundTaper = std::move(other.m_backgroundTaper);
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

void BandProcessor::frequencyShift(const std::vector<float> &background, const std::vector<float> &reference, std::vector<float> &output, const SyncCheckConfig::Band &clearBand)
{
	// Prepare analytic signals for frequency shifting.
	// This combines background spectrum with reference signal and converts it to analytic time domain signal.
	prepareAnalyticSignals(background, reference, m_analyticBackground, m_analyticReference, clearBand);

	// Shift the analytic signal to the target band.
	frequencyShift(m_analyticBackground, m_analyticReference, output);
}

void BandProcessor::prepareAnalyticSignals(const std::vector<float>& background, const std::vector<float>& reference,
										std::vector<kiss_fft_cpx>& analyticBackground, std::vector<kiss_fft_cpx>& analyticReference,
										const SyncCheckConfig::Band &clearBand)
{
	ASSERT_WITH_ERROR(background.size() == m_config.getBlockSize(), "Input background block size must match the configured block size.");
	ASSERT_WITH_ERROR(reference.size() == m_config.getBlockSize(), "Input to shift block size must match the configured block size.");
	const std::size_t fftSize = m_config.getBlockSize();
	analyticBackground.resize(m_config.getHopSize());
	analyticReference.resize(m_config.getHopSize());

	// Perform short time Hilbert transform on both signals
	std::vector<kiss_fft_cpx> backgroundfreq;
	std::vector<kiss_fft_cpx> referencefreq;

	//
	// Create analytic frequency domain signal for background
	//

	m_hilbertShifterBackground->forward(background, backgroundfreq);

	// Clear unwanted bands in the background spectrum
	std::size_t clearStartBin = clearBand.getStartBin(fftSize, m_config.getSampleRate());
	std::size_t clearEndBin   = fftSize / 2 + 1;
	std::size_t clearBinCount = (clearEndBin >= clearStartBin) ? (clearEndBin - clearStartBin + 1) : 0;

	// Recompute the taper window if the number of bands has changed
	if (clearBinCount != m_backgroundTaper.size()) {
		float bandWidthHz = clearBand.getEndFreq() - clearBand.getStartFreq();
		float backgroundTaperFraction = computeTaperFraction(500, bandWidthHz);
		m_backgroundTaper = computeTaperWindow(clearBinCount, backgroundTaperFraction);
	}

	// Apply the taper window to the background spectrum
	std::transform(backgroundfreq.begin(), backgroundfreq.begin() + clearBinCount, m_backgroundTaper.begin(), backgroundfreq.begin(),
				   [](kiss_fft_cpx &val, float windowVal) { return kiss_fft_cpx{val.r * windowVal, val.i * windowVal}; });

	// Inverse the background analytical spectrum to time domain to get the background time domain analyticalsignal.
	m_hilbertShifterBackground->inverse(backgroundfreq, analyticBackground);

	//
	// Create analytic frequency domain signal for reference
	//

	m_hilbertShifterReference->forward(reference, referencefreq);

	// Prepare the reference spectrum for frequency shifting.
	// We will take only required part of the reference spectrum, we will apply taper window to it
	// to minimize the effect of Gibbs phenomenon and clear the rest of the spectrum.
	std::size_t bandStartBin = m_config.getBands()[0].getStartBin(fftSize, m_config.getSampleRate());
	std::size_t bandEndBin   = m_config.getBands()[0].getEndBin(fftSize, m_config.getSampleRate());
	std::size_t shiftBinCount = (bandEndBin >= bandStartBin) ? (bandEndBin - bandStartBin + 1) : 0;

	// Recompute the taper window if the number of bands has changed
	if (shiftBinCount != m_referenceTaper.size()) {
		float bandWidthHz = m_band.getEndFreq() - m_band.getStartFreq();
		float referenceTaperFraction = computeTaperFraction(500, bandWidthHz);
		m_referenceTaper = computeTaperWindow(shiftBinCount, referenceTaperFraction);
	}

	// Apply the taper window to the background spectrum
	std::transform(referencefreq.begin(), referencefreq.begin() + shiftBinCount, m_referenceTaper.begin(), referencefreq.begin(),
				   [](kiss_fft_cpx &val, float windowVal) { return kiss_fft_cpx{val.r * windowVal, val.i * windowVal}; });

	// Clear the rest of the reference spectrum
	std::fill(referencefreq.begin() + shiftBinCount, referencefreq.end(), kiss_fft_cpx{0.0f, 0.0f});

	// Inverse the background spectrum to time domain.
	m_hilbertShifterReference->inverse(referencefreq, analyticReference);
}

void BandProcessor::frequencyShift(const std::vector<kiss_fft_cpx> &analyticBackground, const std::vector<kiss_fft_cpx> &analyticReference, std::vector<float> &output)
{
	// Shift the analytic input signal to the target band.
	m_hilbertShifterReference->shift(analyticReference, output, m_band.getStartFreq());

	// Sum the real part of the background time domain analytic signal and frequency shifted reference signal.
	for (std::size_t i = 0; i < output.size(); i++) {
		output[i] += analyticBackground[i].r;
	}
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
