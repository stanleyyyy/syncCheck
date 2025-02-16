#include "TestSignalGenerator.h"
#include "SyncCheck.h"
#include "Logger.h"
#include <sndfile.h>
#include <iomanip>

TestSignalGenerator::TestSignalGenerator(const SyncCheckConfig &config)
	: m_config(config)
	, m_dist(-0.5f, 0.5f)
	, m_currentSample(0)
	, m_isInitialized(false)
	, m_wavDumpEnabled(false)
	, m_referenceFile(nullptr)
	, m_processingBufferFilled(0)
	, m_processingBuffer(config.getBlockSize())
{
	std::random_device rd;
	m_rng.seed(rd());

	m_delayFrames.resize(m_config.getNumBands());
	m_bandOutputs.resize(m_config.getNumBands());

	// create delay lines for 100ms of delay in both directions
	m_delayLines.resize(m_config.getNumBands());
	for (std::size_t band = 0; band < m_config.getNumBands(); band++) {
		m_delayLines[band] = std::make_unique<DelayLine>(100 * m_config.getSampleRate() / 1000);
	}

	// Sync signal generator via Linear Feedback Shift Register (LFSR). This generates a pseudo-random sequence of bits
	// with a defineable starting position (or a timestamps) which guarantees that the sequence is the same for all runs.
	// The pseudo-random sequence is generated at 16x lower rate than the sampling rate, to save CPU cycles.
	m_syncSignalGenerator = std::make_unique<SyncSignalGenerator<float>>(m_config.getSampleRate(), m_config.getSampleRate() / 16, 24);
}

TestSignalGenerator::~TestSignalGenerator()
{
	closeWavFiles();
}

void TestSignalGenerator::closeWavFiles()
{
	for (SNDFILE *file : m_wavFiles) {
		if (file) {
			sf_close(file);
		}
	}
	m_wavFiles.clear();
	if (m_referenceFile) {
		sf_close(m_referenceFile);
		m_referenceFile = nullptr;
	}
	m_wavDumpEnabled = false;
}

bool TestSignalGenerator::enableWavDump(bool enable, const std::string &prefix)
{
	// If already in desired state, do nothing
	if (enable == m_wavDumpEnabled) {
		return true;
	}

	// If disabling, close files
	if (!enable) {
		closeWavFiles();
		return true;
	}

	// Close any existing files
	closeWavFiles();

	// Open new files for each band
	SF_INFO sf_info;
	sf_info.samplerate = m_config.getSampleRate();
	sf_info.channels = 1;
	sf_info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
	sf_info.sections = 1;
	sf_info.seekable = 1;

	m_wavFiles.resize(m_config.getNumBands() + 1); // +1 for mixed signal
	bool success = true;

	for (std::size_t band = 0; band < m_config.getNumBands(); band++) {
		std::string filename = prefix + "_band" + std::to_string(band) + ".wav";
		m_wavFiles[band] = sf_open(filename.c_str(), SFM_WRITE, &sf_info);
		if (!m_wavFiles[band]) {
			success = false;
			break;
		}
	}

	// Open mixed signal file
	if (success) {
		std::string filename = prefix + "_mixed.wav";
		m_wavFiles[m_config.getNumBands()] = sf_open(filename.c_str(), SFM_WRITE, &sf_info);
		if (!m_wavFiles[m_config.getNumBands()]) {
			success = false;
		}
	}

	// Open reference signal file
	if (success) {
		std::string filename = prefix + "_reference.wav";
		m_referenceFile = sf_open(filename.c_str(), SFM_WRITE, &sf_info);
		if (!m_referenceFile) {
			success = false;
		}
	}

	if (!success) {
		closeWavFiles();
		return false;
	}

	m_wavDumpEnabled = true;
	return true;
}

void TestSignalGenerator::init(std::vector<float> &delays)
{
	if (delays.size() != m_config.getNumBands()) {
		throw std::runtime_error("Delays array must match number of bands");
	}

	for (std::size_t band = 0; band < m_config.getNumBands(); band++) {
		// calculate delay in frames
		m_delayFrames[band] = static_cast<int>(roundf(delays[band] * m_config.getSampleRate() / 1000));
		// recalculate actual delay
		delays[band] = 1000.0f * m_delayFrames[band] / m_config.getSampleRate();
	}

	m_currentSample = 0;
	m_isInitialized = true;
	m_metrics = QualityMetrics {};

	// Create band processors
	m_bandProcessors.clear();
	for (std::size_t band = 0; band < m_config.getNumBands(); band++) {
		m_bandProcessors.emplace_back(m_config, m_config.getBands()[band]);
	}
}

void TestSignalGenerator::generateReferenceSignal(std::vector<float> &output, std::size_t numFrames)
{
	if (m_syncSignalGenerator) {
		m_syncSignalGenerator->generateSyncStream(output, m_currentSample);

		// scale down the amplitude to 10% of the original amplitude
		std::for_each(output.begin(), output.end(), [](float &value) { value = value * 0.1f; });
	}
	else {
		// Generate new data starting after the hop
		for (std::size_t i = 0; i < numFrames; i++) {
			output[i] = m_dist(m_rng) * 0.2f; // Reduce noise amplitude
		}
	}

	// Generate a sine wave with a frequency of 0.3 Hz
	const float freq = 0.3f;
	const float sample_rate = m_config.getSampleRate();

	// Add a pulse every second (width: 10ms, amplitude: 1.0)
	for (std::size_t i = 0; i < numFrames; i++) {
		int sample_in_second = (m_currentSample + i) % m_config.getSampleRate();
		if (sample_in_second < static_cast<int>(0.01f * m_config.getSampleRate())) {
			float pulse = 0.5f * (1.0f - static_cast<float>(sample_in_second) / (0.01f * m_config.getSampleRate()));
			output[i] += pulse;
		}

		// Generate sine wave for testing
		output[i] += 0.2 * std::sin(2.0f * M_PI * freq * (m_currentSample + i) / sample_rate);
	}

	m_currentSample += numFrames;
}

void TestSignalGenerator::renderAudioBlock(std::vector<float> &output)
{
	// make sure all work buffers are the same size as the block size
	if (m_processingBuffer.size() != m_config.getBlockSize()) {
		m_processingBuffer.resize(m_config.getBlockSize());
	}

	if (m_referenceBuffer.size() != m_config.getBlockSize()) {
		m_referenceBuffer.resize(m_config.getBlockSize());
	}

	if (m_backgroundBuffer.size() != m_config.getBlockSize()) {
		m_backgroundBuffer.resize(m_config.getBlockSize());
	}

	std::size_t processed = 0;

	while (processed < output.size()) {

		// How many samples from processing buffer can we copy to output?
		size_t samplesToProcess = std::min(output.size() - processed, m_processingBufferFilled);
		std::copy(m_processingBuffer.begin(), m_processingBuffer.begin() + samplesToProcess, output.begin() + processed);

		// consume 'samplesToProcess' worth of samples from the buffer
		std::copy(m_processingBuffer.begin() + samplesToProcess, m_processingBuffer.begin() + m_processingBufferFilled, m_processingBuffer.begin());
		m_processingBufferFilled -= samplesToProcess;
		processed += samplesToProcess;

		// Generate new hopSize worth of reference signal only if we consumed everything from m_processingBuffer
		if (m_processingBufferFilled == 0) {
			std::vector<float> referenceData(m_config.getHopSize());
			generateReferenceSignal(referenceData, m_config.getHopSize());

			// Optionally, write the reference signal to WAV.
			if (m_wavDumpEnabled && m_referenceFile) {
				sf_writef_float(m_referenceFile, referenceData.data(), m_config.getHopSize());
			}

			// Shift the existing buffers left by hopSize.
			std::copy(m_backgroundBuffer.begin() + m_config.getHopSize(), m_backgroundBuffer.end(), m_backgroundBuffer.begin());
			std::copy(m_referenceBuffer.begin() + m_config.getHopSize(), m_referenceBuffer.end(), m_referenceBuffer.begin());

			// Append the new hop to the processingBuffer and update backgroundBuffer.
			std::copy(referenceData.begin(), referenceData.end(), m_referenceBuffer.end() - m_config.getHopSize());
			std::fill(m_backgroundBuffer.end() - m_config.getHopSize(), m_backgroundBuffer.end(), 0.0f);

			// Compute FFT of the full processing and background buffers.
			std::vector<kiss_fft_cpx> backgroundFreq;
			m_bandProcessors[0].forward(m_backgroundBuffer, backgroundFreq);

			std::vector<kiss_fft_cpx> referenceFreq;
			m_bandProcessors[0].forward(m_referenceBuffer, referenceFreq);

			// Define clearBand covering all bands.
			const SyncCheckConfig::Band clearBand = { m_config.getBands()[0].getStartFreq(), m_config.getBands()[m_config.getNumBands() - 1].getEndFreq() };

			// Generate each band of test signal by shifting the FFT data of the reference signal
			for (std::size_t band = 0; band < m_config.getNumBands(); band++) {
				m_bandProcessors[band].processBlockWithShiftFFT(backgroundFreq, referenceFreq, m_bandOutputs[band], clearBand);
			}

			// Process delay lines and optionally write each band's hop to WAV.
			for (std::size_t band = 0; band < m_config.getNumBands(); band++) {
				m_delayLines[band]->processBlock(m_bandOutputs[band], m_bandOutputs[band], m_config.getHopSize(), m_delayFrames[band]);
				if (m_wavDumpEnabled && m_wavFiles[band]) {
					sf_writef_float(m_wavFiles[band], m_bandOutputs[band].data(), m_config.getHopSize());
				}
			}

			// Mix the bands for the current hop.
			for (std::size_t i = 0; i < m_config.getHopSize(); i++) {
				float mix = 0.0f;
				for (std::size_t band = 0; band < m_config.getNumBands(); band++) {
					mix += m_bandOutputs[band][i];
				}
				m_processingBuffer[i] = mix / static_cast<float>(m_config.getNumBands());
			}

			// we have now a full hop's worth of data in m_processingBuffer
			m_processingBufferFilled = m_config.getHopSize();

			// Optionally, write the mixed hop to WAV.
			if (m_wavDumpEnabled && m_wavFiles[m_config.getNumBands()]) {
				sf_writef_float(m_wavFiles[m_config.getNumBands()], &m_processingBuffer[0], m_config.getHopSize());
			}
		}
	}
}

std::vector<float> TestSignalGenerator::renderTestSignal(float duration, std::vector<float> &delays_ms, bool verbose)
{
	// Initialize block generation
	init(delays_ms);

	std::size_t numFrames = static_cast<int>(duration * m_config.getSampleRate());

	numFrames /= m_config.getBlockSize();
	numFrames *= m_config.getBlockSize();
	std::vector<float> mixed_signal(numFrames);

	if (verbose) {
		DBG_INFO("Generating test signal:");
		DBG_INFO_INLINE("[");
	}
	int last_percent = 0;

	// Generate signal in blocks
	for (std::size_t offset = 0; offset < numFrames;) {
		if (verbose) {
			int percent = (offset * 100) / numFrames;
			if (percent > last_percent) {
				for (int i = last_percent; i < percent; i += 2) {
					DBG_INFO_INLINE("=");
					fflush(stdout);
				}
				last_percent = percent;
			}
		}

		std::size_t block_size = std::min((int)m_config.getBlockSize(), (int)(numFrames - offset));
		std::vector<float> block(block_size);
		renderAudioBlock(block);
		std::copy(block.begin(), block.end(), mixed_signal.begin() + offset);
		offset += block_size;
	}

	if (verbose) {
		for (int i = last_percent; i <= 100; i += 2) {
			DBG_INFO_INLINE("=");
		}
		DBG_INFO_INLINE("] 100%" << "\n");
		m_metrics.print();
	}

	return mixed_signal;
}

void TestSignalGenerator::QualityMetrics::print() const
{
	DBG_INFO("Signal Quality Metrics:");
	DBG_INFO("  SNR: " << std::fixed << std::setprecision(2) << m_snr << " dB");
	DBG_INFO("  Band Isolation: " << std::fixed << std::setprecision(2) << m_bandIsolation << " dB");
	DBG_INFO("  Phase Coherence: " << std::fixed << std::setprecision(2) << m_phaseCoherence);
}