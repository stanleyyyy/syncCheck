#include "TestSignalGenerator.h"
#include "SyncCheck.h"
#include "Logger.h"
#include <sndfile.h>
#include <assert.h>
#include <iomanip>

TestSignalGenerator::TestSignalGenerator(const SyncCheckConfig& config)
	: m_config(config)
	, m_dist(-0.5f, 0.5f)
	, m_currentSample(0)
	, m_isInitialized(false)
	, m_wavDumpEnabled(false)
{
	std::random_device rd;
	m_rng.seed(rd());

	m_delayFrames.resize(m_config.getNumBands());

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
	for (SNDFILE* file : m_wavFiles) {
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

bool TestSignalGenerator::enableWavDump(bool enable, const std::string& prefix)
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

void TestSignalGenerator::init(std::vector<float>& delays)
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
	m_metrics = QualityMetrics{};

	// Initialize processing buffer with zeros
	m_processingBuffer.resize(m_config.getBlockSize());
	std::fill(m_processingBuffer.begin(), m_processingBuffer.end(), 0.0f);

	m_backgroundBuffer.resize(m_config.getBlockSize());
	std::fill(m_backgroundBuffer.begin(), m_backgroundBuffer.end(), 0.0f);

	// Initialize band signals with zeros
	m_bandSignals.resize(m_config.getNumBands());
	for (std::size_t band = 0; band < m_config.getNumBands(); band++) {
		m_bandSignals[band].resize(m_config.getBlockSize());
		std::fill(m_bandSignals[band].begin(), m_bandSignals[band].end(), 0.0f);
	}

	// Create band processors
	m_bandGenerator.clear();
	for (std::size_t band = 0; band < m_config.getNumBands(); band++) {
		m_bandGenerator.emplace_back(m_config, m_config.getBands()[band]);
	}
}

void TestSignalGenerator::generateReferenceSignal(std::vector<float>& output, std::size_t numFrames)
{
	if (m_syncSignalGenerator) {
		m_syncSignalGenerator->generateSyncStream(output, m_currentSample);

		// scale down the amplitude to 10% of the original amplitude
		std::for_each(output.begin(), output.end(), [](float& value) {
			value = value * 0.1f;
		});
	} else {
		// Generate new data starting after the hop
		for (std::size_t i = 0; i < numFrames; i++) {
			output[i] = m_dist(m_rng) * 0.2f; // Reduce noise amplitude
		}
	}

	// Add a pulse every second (width: 10ms, amplitude: 1.0)
	for (std::size_t i = 0; i < numFrames; i++) {
		int sample_in_second = (m_currentSample + i) % m_config.getSampleRate();
		if (sample_in_second < static_cast<int>(0.01f * m_config.getSampleRate())) {
			float pulse = 0.5f * (1.0f - static_cast<float>(sample_in_second) / (0.01f * m_config.getSampleRate()));
			output[i] += pulse;
		}

		// Generate sine wave for testing
		float freq = 0.3f; // 440 Hz
		float sample_rate = m_config.getSampleRate();
		static float phase = 0.0f;

		output[i] += 0.02f * sinf(2.0f * M_PI * freq * phase);
		phase += 1.0f / sample_rate;
		if (phase >= 1.0f) {
			phase -= 1.0f;
		}
	}

	m_currentSample += numFrames;
}

bool TestSignalGenerator::renderAudioBlock(std::vector<float>& output, std::size_t numFrames)
{
	assert(output.size() == m_config.getBlockSize());

	if (!m_isInitialized) {
		throw std::runtime_error("Block generation not initialized. Call initBlockGeneration first.");
	}

	std::fill(output.begin(), output.end(), 0.0f);

	// Process the signal
	for (std::size_t offset = 0; offset < numFrames; offset += m_config.getHopSize()) {

		// Generate fresh reference signal data (for m_config.getHopSize() frames)
		std::vector<float> referenceData(m_config.getHopSize());
		generateReferenceSignal(referenceData, m_config.getHopSize());

		// Write reference signal to WAV file if enabled
		if (m_wavDumpEnabled && m_referenceFile) {
			sf_writef_float(m_referenceFile, referenceData.data(), m_config.getHopSize());
		}

		// Shift existing data left in processing buffer
		std::copy(m_processingBuffer.begin() + m_config.getHopSize(), m_processingBuffer.end(), m_processingBuffer.begin());
		std::copy(m_backgroundBuffer.begin() + m_config.getHopSize(), m_backgroundBuffer.end(), m_backgroundBuffer.begin());

		// Copy new hop data to the end of processing buffer
		std::copy(referenceData.begin(), referenceData.end(), m_processingBuffer.end() - m_config.getHopSize());

		// And fill silence to background buffer (for now)
		std::fill(m_backgroundBuffer.end() - m_config.getHopSize(), m_backgroundBuffer.end(), 0.0f);

		// Get windowed FFT of background signal
		std::vector<kiss_fft_cpx> background_freqData(m_config.getBlockSize() / 2 + 1);
		m_bandGenerator[0].applyWindowAndFFT(m_backgroundBuffer, background_freqData);

		// Get windowed FFT of processing buffer
		std::vector<kiss_fft_cpx> band0_freqData(m_config.getBlockSize() / 2 + 1);
		m_bandGenerator[0].applyWindowAndFFT(m_processingBuffer, band0_freqData);

		// Low/high bins covering all the bands
		const std::size_t lowestBandStartBin = m_config.getBands()[0].m_startBin;
		const std::size_t highestBandEndBin = m_config.getBands()[m_config.getNumBands() - 1].m_endBin;

		// Process each band with full block size
		for (std::size_t band = 0; band < m_config.getNumBands(); band++) {

			// generate single band output (using FFT of processing buffer as source)
			m_bandGenerator[band].processBlockWithShiftFFT(background_freqData, band0_freqData, m_bandSignals[band], lowestBandStartBin, highestBandEndBin);

			// We have now valid first m_config.getHopSize() frames in block_output.
			// Delay them and mix them to the output signal
			m_delayLines[band]->processBlock(m_bandSignals[band], m_bandSignals[band], m_config.getHopSize(), m_delayFrames[band]);

			// Write band signal to WAV file if enabled
			if (m_wavDumpEnabled && m_wavFiles[band]) {
				sf_writef_float(m_wavFiles[band], m_bandSignals[band].data(), m_config.getHopSize());
			}

			// Mix the band signal into the output signal
			for (std::size_t i = 0; i < m_config.getHopSize(); i++) {
				output[offset + i] += m_bandSignals[band][i] / m_config.getNumBands();
			}
		}

		// Write mixed signal to WAV file if enabled
		if (m_wavDumpEnabled && m_wavFiles[m_config.getNumBands()]) {
			sf_writef_float(m_wavFiles[m_config.getNumBands()], &output[offset], m_config.getHopSize());
		}
	}

	// Update quality metrics
	float total_isolation = 0.0f;
	for (std::size_t band = 0; band < m_config.getNumBands(); band++) {
		total_isolation += m_bandGenerator[band].getBandIsolation();
	}
	m_metrics.m_bandIsolation = total_isolation / m_config.getNumBands();

	float signal_power = 0.0f;
	float noise_power = 1e-10f;
	for (std::size_t band = 0; band < m_config.getNumBands(); band++) {
		for (std::size_t i = 0; i < m_config.getHopSize(); i++) {
			signal_power += m_bandSignals[band][i] * m_bandSignals[band][i];
		}
	}
	m_metrics.m_snr = 10.0f * log10f(signal_power / noise_power);

	float total_coherence = 0.0f;
	for (std::size_t band = 0; band < m_config.getNumBands() - 1; band++) {
		float max_correlation = 0.0f;
		std::size_t start_idx = numFrames / 4;
		std::size_t end_idx = 3 * numFrames / 4;
		for (int lag = -50; lag <= 50; lag++) {
			float correlation = 0.0f;
			std::size_t count = 0;
			for (std::size_t i = start_idx; i < end_idx; i++) {
				int j = i + lag;
				if (j >= 0 && j < (int)numFrames) {
					correlation += m_bandSignals[band][i] * m_bandSignals[band + 1][j];
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
	m_metrics.m_phaseCoherence = total_coherence / (m_config.getNumBands() - 1);
	return true;
}

std::vector<float> TestSignalGenerator::renderTestSignal(float duration, std::vector<float>& delays_ms, bool verbose)
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
		renderAudioBlock(block, block_size);
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