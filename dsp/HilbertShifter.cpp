#include "HilbertShifter.h"
#include "Logger.h"

HilbertShifter::HilbertShifter(int blockSize, int hopSize, float sampleRate)
	: m_stftProc(std::make_unique<STFTProcessor>(blockSize, hopSize))
	, m_hopSize(hopSize)
	, m_sampleRate(sampleRate)
	, m_phase(0.0)
{
	ASSERT_WITH_ERROR(blockSize > 0 && hopSize > 0, "Block size and hop size must be positive");
	ASSERT_WITH_ERROR(hopSize == blockSize / 2 || hopSize == blockSize / 4, "Hop size must be blockSize/2 or blockSize/4");

	m_hilbertOutput.resize(hopSize);
}

void HilbertShifter::computeAnalyticHalfSpectrum(const std::vector<kiss_fft_cpx> &frame, std::vector<kiss_fft_cpx> &analytic)
{
	int M = frame.size(); // fftSize/2+1

	// Copy the input frame to the output analytic half-spectrum
	analytic.resize(frame.size());
	std::copy(frame.begin(), frame.end(), analytic.begin());

	// Scale the real and imaginary parts by 2.0f for the analytic half-spectrum
	if (M > 1) {
		for (int k = 1; k < M - 1; k++) {
			analytic[k].r *= 2.0f;
			analytic[k].i *= 2.0f;
		}
	}
}

void HilbertShifter::halfToFullAnalytic(const std::vector<kiss_fft_cpx> &half, std::vector<kiss_fft_cpx> &full)
{
	const std::size_t M = half.size(); // should equal fftSize/2+1
	const std::size_t fftSize = m_stftProc->getSize();

	full.resize(fftSize);
	for (std::size_t k = 0; k < M; k++) {
		full[k] = half[k];
	}
	for (std::size_t k = M; k < fftSize; k++) {
		full[k].r = 0;
		full[k].i = 0;
	}
}

void HilbertShifter::forward(const std::vector<float> &input, std::vector<kiss_fft_cpx> &output)
{
	// Forward STFT
	auto frames = m_stftProc->forward(input);
	if (frames.empty()) {
		return;
	}

	// Convert to analytic spectrum
	auto halfFrame = frames[0];
	computeAnalyticHalfSpectrum(halfFrame, m_analyticHalf);
	halfToFullAnalytic(m_analyticHalf, output);
}

void HilbertShifter::inverse(const std::vector<kiss_fft_cpx> &input, std::vector<kiss_fft_cpx> &output)
{
	// Get complex time domain signal (Hilbert transform)
	m_stftProc->complexProcessFrame(input, output);
}

void HilbertShifter::transform(const std::vector<float> &input, std::vector<kiss_fft_cpx> &output, std::function<void(std::vector<kiss_fft_cpx>&)> modifySpectrumCb)
{
	forward(input, m_fullAnalytic);

	// Modify the spectrum if a callback is provided
	if (modifySpectrumCb) {
		modifySpectrumCb(m_fullAnalytic);
	}

	inverse(m_fullAnalytic, output);
}

void HilbertShifter::shift(const std::vector<kiss_fft_cpx> &input, std::vector<float> &output, float freqOffset)
{
	// Frequency shift via complex modulation
	output.resize(m_hopSize);
	double w0 = 2.0 * M_PI * freqOffset / m_sampleRate;

	for (int i = 0; i < m_hopSize; i++) {
		double theta = w0 * i + m_phase;
		double c = cos(theta);
		double s = sin(theta);

		// Real part = (reA*c - imA*s), scale by 2
		output[i] = 2.0f * (input[i].r * c - input[i].i * s);
	}

	// Update phase for next block
	m_phase = fmod(m_phase + w0 * m_hopSize, 2.0 * M_PI);
}

void HilbertShifter::transformWithShift(const std::vector<float> &input, std::vector<float> &output, float freqOffset)
{
	// Compute Hilbert transform - the output is the complex time domain signal
	transform(input, m_hilbertOutput);

	// Shift the complex time domain signal
	shift(m_hilbertOutput, output, freqOffset);
}