#include "SyncAnalyzer.h"
#include "AudioInput.h"
#include "AudioOutput.h"
#include "Logger.h"
#include "WavFile.h"
#include <algorithm>
#include <memory>
#include <sstream>
#include <thread>

bool SyncAnalyzer::parseSources(const std::string &sources, unsigned int channels,
	std::vector<FFTSyncCheck::ChannelAssignment> &assignments, std::string &error)
{
	assignments.clear();

	std::istringstream srcStream(sources);
	std::string token;
	std::size_t refChannel = 0;
	std::size_t refBand = 0;
	while (std::getline(srcStream, token, ',')) {
		if (token.empty())
			continue;
		size_t pos = token.find(':');
		int ph = 0, band = 0;
		try {
			if (pos != std::string::npos) {
				ph = std::stoi(token.substr(0, pos));
				band = std::stoi(token.substr(pos + 1));
			} else {
				ph = std::stoi(token);
				band = 0;
			}
		} catch (const std::exception &) {
			error = "invalid source mapping '" + token + "'";
			return false;
		}
		if (ph < 0 || (unsigned int)ph >= channels) {
			error = "source channel " + std::to_string(ph) + " out of range (" + std::to_string(channels) + " channels)";
			return false;
		}
		if (band < 0) {
			error = "invalid band in source mapping '" + token + "'";
			return false;
		}
		FFTSyncCheck::ChannelAssignment assign;
		assign.sigChannel = ph;
		assign.sigBand = band;
		// The first channel/band pair specifies the reference for all assignments.
		if (assignments.empty()) {
			refChannel = ph;
			refBand = band;
		}
		assign.refChannel = refChannel;
		assign.refBand = refBand;
		assignments.push_back(assign);

		if (assignments.size() == channels) {
			break;
		}
	}

	if (assignments.empty()) {
		error = "empty source mapping";
		return false;
	}
	return true;
}

bool SyncAnalyzer::validate(const SyncAnalyzerSettings &settings, std::string &error)
{
	const unsigned int blockSize = settings.m_blockSize;
	if (blockSize < 256 || (blockSize & (blockSize - 1)) != 0) {
		error = "block size must be a power of two of at least 256";
		return false;
	}
	if (settings.m_overlapPercent != 50 && settings.m_overlapPercent != 75) {
		error = "overlap must be 50 or 75 percent";
		return false;
	}
	if (settings.m_interpFactor < 1 || settings.m_interpFactor > 16) {
		error = "interpolation factor must be between 1 and 16";
		return false;
	}
	if (settings.m_statsRefreshRate <= 0.0f) {
		error = "update interval must be positive";
		return false;
	}
	if (settings.m_mode == SyncAnalyzerMode::Channels) {
		if (settings.m_minFreq >= settings.m_maxFreq) {
			error = "the minimum frequency must be below the maximum frequency";
			return false;
		}
	} else {
		const unsigned int minBands = settings.m_sources == "mix" ? 2 : SyncCheckConfig::MIN_BANDS;
		if (settings.m_numBands < minBands || settings.m_numBands > (unsigned int)SyncCheckConfig::MAX_BANDS) {
			error = "the number of bands must be between " + std::to_string(minBands) + " and " + std::to_string(SyncCheckConfig::MAX_BANDS);
			return false;
		}
		if (settings.m_freqRange / settings.m_numBands <= settings.m_bandSep) {
			error = "the band separation is larger than the band width";
			return false;
		}
	}
	return true;
}

bool SyncAnalyzer::resolveSettings(const SyncAnalyzerSettings &settings, unsigned int channels, unsigned int sampleRate,
	SyncAnalyzerSettings &resolved, std::string &error)
{
	resolved = settings;

	if (settings.m_mode == SyncAnalyzerMode::Sources) {
		if (settings.m_sources == "mix" && settings.m_numBands < 2) {
			error = "mix mode needs at least 2 bands";
			return false;
		}
		return true;
	}

	// Channel mode: compare the reference channel with the measured channels over one wide band.
	if (channels < 2) {
		error = "channel mode needs at least 2 input channels, the input has " + std::to_string(channels);
		return false;
	}
	if (settings.m_referenceChannel >= channels) {
		error = "reference channel " + std::to_string(settings.m_referenceChannel) + " does not exist ("
				+ std::to_string(channels) + " channels)";
		return false;
	}
	if (settings.m_minFreq >= settings.m_maxFreq || settings.m_maxFreq > sampleRate / 2) {
		error = "invalid frequency range " + std::to_string(settings.m_minFreq) + " - " + std::to_string(settings.m_maxFreq)
				+ " Hz (Nyquist is " + std::to_string(sampleRate / 2) + " Hz)";
		return false;
	}

	std::string sources = std::to_string(settings.m_referenceChannel);
	std::vector<unsigned int> used = { settings.m_referenceChannel };
	for (unsigned int ch = 0; ch < channels; ch++) {
		const bool measured = settings.m_measuredChannels.empty()
			|| std::find(settings.m_measuredChannels.begin(), settings.m_measuredChannels.end(), ch) != settings.m_measuredChannels.end();
		if (measured && std::find(used.begin(), used.end(), ch) == used.end()) {
			used.push_back(ch);
			sources += "," + std::to_string(ch);
		}
	}
	for (unsigned int ch : settings.m_measuredChannels) {
		if (ch >= channels) {
			error = "channel " + std::to_string(ch) + " does not exist (" + std::to_string(channels) + " channels)";
			return false;
		}
	}
	if (used.size() < 2) {
		error = "no channel to compare with the reference channel";
		return false;
	}

	resolved.m_sources = sources;
	resolved.m_numBands = 1;
	resolved.m_startFreq = settings.m_minFreq;
	resolved.m_freqRange = settings.m_maxFreq - settings.m_minFreq;
	resolved.m_bandSep = 0;
	return true;
}

bool SyncAnalyzer::setupChecker(const SyncAnalyzerSettings &settings, unsigned int sampleRate, unsigned int channels,
	CheckerSetup &setup, std::string &error)
{
	setup.m_multichannel = (settings.m_sources != "mix");

	// In mix mode we process a single mono channel.
	// Otherwise, we will build a custom channel assignment based on the source mapping.
	setup.m_config = SyncCheckConfig(settings.m_blockSize, settings.m_overlapPercent, sampleRate, settings.m_numBands, settings.m_interpFactor);
	setup.m_config.initBands(settings.m_startFreq, settings.m_freqRange, settings.m_bandSep);
	setup.m_config.setCorrelationDumpEnabled(settings.m_dumpCorrelation);

	// Setup channel assignments. 'assignments' uses physical channels, 'checkerAssignments'
	// uses the columns of the processing buffer that FFTSyncCheck works on.
	std::vector<FFTSyncCheck::ChannelAssignment> &assignments = setup.m_assignments;
	std::vector<FFTSyncCheck::ChannelAssignment> &checkerAssignments = setup.m_checkerAssignments;
	assignments.clear();
	checkerAssignments.clear();
	if (!setup.m_multichannel)
	{
		// Create default channel assignments for mix mode.
		// In mix mode, there is a single input channel (channel 0).
		// We assign each band (except the reference band, assumed to be band 0) as its own processing assignment.
		for (std::size_t band = 1; band < settings.m_numBands; band++) {
			FFTSyncCheck::ChannelAssignment assign;
			assign.refChannel = 0;
			assign.sigChannel = 0;
			assign.sigBand = band;
			assign.refBand = 0;
			assignments.push_back(assign);
		}
		checkerAssignments = assignments;
	}
	else
	{
		if (!parseSources(settings.m_sources, channels, assignments, error)) {
			return false;
		}
		for (const auto &assign : assignments) {
			if (assign.sigBand >= settings.m_numBands) {
				error = "source band " + std::to_string(assign.sigBand) + " out of range (" + std::to_string(settings.m_numBands) + " bands)";
				return false;
			}
		}
		// Column m of the processing buffer holds the signal channel of assignment m,
		// column 0 holds the reference.
		for (std::size_t m = 0; m < assignments.size(); m++) {
			checkerAssignments.push_back({ 0, assignments[m].refBand, m, assignments[m].sigBand });
		}
	}

	setup.m_procChannels = setup.m_multichannel ? (unsigned int)assignments.size() : 1;
	return true;
}

void SyncAnalyzer::fillProcessingBuffer(const CheckerSetup &setup, const std::vector<float> &inputBuffer, unsigned int channels,
	std::vector<float> &processingBuffer)
{
	// How many frames have we got?
	const std::size_t numFrames = inputBuffer.size() / channels;
	const unsigned int procChannels = setup.m_procChannels;
	const auto &assignments = setup.m_assignments;

	if (setup.m_multichannel) {
		// In non-mix mode, inputBuffer is interleaved over all physical channels.
		processingBuffer.resize(numFrames * procChannels);
		// For each frame, extract only the channels needed as defined by assignments.
		for (unsigned int i = 0; i < numFrames; i++) {
			for (unsigned int m = 0; m < assignments.size(); m++) {
				// Use the physical signal channel defined in the assignment.
				std::size_t ph = assignments[m].sigChannel;
				processingBuffer[(i) * procChannels + m] = inputBuffer[i * channels + ph];
			}
		}
	}
	else {
		// Mix mode. Mix all channels together.
		processingBuffer.resize(numFrames);
		std::fill(processingBuffer.begin(), processingBuffer.end(), 0.0f);
		for (unsigned int j = 0; j < channels; j++) {
			for (unsigned int i = 0; i < numFrames; i++) {
				processingBuffer[i] += inputBuffer[i * channels + j] / channels;
			}
		}
	}
}

void SyncAnalyzer::collectResults(const CheckerSetup &setup, const FFTSyncCheck &syncChecker, SyncAnalyzerSnapshot &snapshot)
{
	snapshot.m_results.clear();
	const auto reports = syncChecker.getDelayStats();
	for (std::size_t i = 0; i < reports.size() && i < setup.m_assignments.size(); i++) {
		const auto &assign = setup.m_checkerAssignments[i];
		// Skip self-assignments
		if (assign.refChannel == assign.sigChannel && assign.refBand == assign.sigBand) {
			continue;
		}
		SyncAnalyzerResult result { reports[i], syncChecker.getCorrelationData(assign) };
		result.m_report.assignment = setup.m_assignments[i];
		snapshot.m_results.push_back(result);
	}
}

bool SyncAnalyzer::run(const SyncAnalyzerSettings &requestedSettings, const StatsCallback &onStats, std::string &error)
{
	m_stopRequested = false;

	// Initialize capture device.
	std::unique_ptr<AudioSource> capture;
	if (requestedSettings.m_captureDevice.find("file://") == 0) {
		capture = std::make_unique<FileCapture>(requestedSettings.m_captureDevice, requestedSettings.m_periodSize, requestedSettings.m_realtimeFile);
	} else {
		capture = std::make_unique<DeviceCapture>(requestedSettings.m_captureDevice,
			requestedSettings.m_channels,
			requestedSettings.m_sampleRate,
			requestedSettings.m_periodSize,
			requestedSettings.m_periods);
	}
	if (!capture->isInitialized()) {
		error = "failed to open capture device " + requestedSettings.m_captureDevice;
		if (!capture->getError().empty()) {
			error += ": " + capture->getError();
		}
		return false;
	}

	// After capture initialization, take sample rate and channels from the actual source.
	const unsigned int sampleRate = capture->getSampleRate();
	const unsigned int channels = capture->getChannels();

	// Channel mode is translated into an equivalent source mapping.
	SyncAnalyzerSettings resolvedSettings;
	if (!resolveSettings(requestedSettings, channels, sampleRate, resolvedSettings, error)) {
		return false;
	}
	const SyncAnalyzerSettings &settings = resolvedSettings;

	// Initialize playback device if requested.
	std::unique_ptr<DevicePlayback> playback;
	if (!settings.m_playbackDevice.empty()) {
		playback = std::make_unique<DevicePlayback>(settings.m_playbackDevice,
			channels,
			sampleRate,
			settings.m_periodSize,
			settings.m_periods);
		if (!playback->isInitialized()) {
			error = "failed to initialize audio output device " + settings.m_playbackDevice;
			return false;
		}
		DBG_INFO("Audio output enabled on device: " << settings.m_playbackDevice);
	}

	DBG_INFO("Starting capture from device: " << settings.m_captureDevice);
	DBG_INFO("Sample rate: " << sampleRate);
	DBG_INFO("Physical channels: " << channels);
	DBG_INFO("Block size: " << settings.m_blockSize);
	DBG_INFO("Overlap: " << settings.m_overlapPercent << "%");
	DBG_INFO("Number of bands: " << settings.m_numBands);
	DBG_INFO("Interpolation factor: " << settings.m_interpFactor);
	DBG_INFO("Start frequency: " << settings.m_startFreq);
	DBG_INFO("Frequency range: " << settings.m_freqRange);
	DBG_INFO("Band separation: " << settings.m_bandSep);
	DBG_INFO("Sources mapping: " << settings.m_sources);

	CheckerSetup setup;
	if (!setupChecker(settings, sampleRate, channels, setup, error)) {
		return false;
	}

	// Initialize FFTSyncCheck.
	FFTSyncCheck syncChecker(setup.m_config);
	syncChecker.setMultiChannelMode(setup.m_procChannels);
	syncChecker.setChannelAssignments(setup.m_checkerAssignments);

	// Allocate processing buffer.
	// For mixing mode, we expect blockSize samples.
	// For per-channel processing, we need blockSize frames x (number of assignments).
	std::vector<float> processingBuffer(settings.m_blockSize * setup.m_procChannels, 0.0f);

	float statsUpdateTime = 0.0f;
	float totalTime = 0.0f;
	uint64_t totalFrames = 0;
	SyncAnalyzerSnapshot snapshot;
	snapshot.m_sampleRate = sampleRate;
	snapshot.m_interpFactor = settings.m_interpFactor;
	snapshot.m_channels = channels;
	snapshot.m_mode = settings.m_mode;

	// Record the captured audio if requested
	WavRecorder recorder;
	if (!settings.m_recordFile.empty()) {
		if (!recorder.open(settings.m_recordFile, channels, sampleRate)) {
			error = "failed to open recording file " + settings.m_recordFile;
			return false;
		}
		DBG_INFO("Recording audio to: " << settings.m_recordFile);
	}

	// Initialize WAV dump file if requested
	WavWriter dumpFile;
	if (settings.m_dumpAudio) {
		// Create filename with configuration parameters
		std::stringstream ss;
		ss << "dump_"
		   << settings.m_startFreq << "hz_"
		   << settings.m_freqRange << "range_"
		   << settings.m_bandSep << "sep_"
		   << settings.m_numBands << "bands.wav";
		std::string dumpFileName = ss.str();

		if (!dumpFile.open(dumpFileName, channels, sampleRate)) {
			error = "failed to open dump file " + dumpFileName;
			return false;
		}
		DBG_INFO("Dumping audio to: " << dumpFileName);
	}

	// If playback is enabled, kick it off with writing a full block of zeros.
	if (playback) {
		std::vector<float> zeroBlock(settings.m_blockSize * channels, 0.0f);
		playback->playBlock(zeroBlock);
	}

	// Unblock the capture when stop() is requested from another thread.
	std::atomic<bool> finished { false };
	std::thread stopWatcher([&] {
		while (!finished && !m_stopRequested) {
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
		capture->stop();
	});

	while (!m_stopRequested) {
		std::vector<float> inputBuffer = capture->captureBlock();
		if (inputBuffer.empty()) {
			if (!m_stopRequested) {
				DBG_INFO("End of input reached.");
			}
			break;
		}

		// If playback is enabled, write the captured block to the audio output.
		if (playback) {
			playback->playBlock(inputBuffer);
		}

		// If dumping is enabled, write the (raw) new samples.
		if (dumpFile.isOpen()) {
			dumpFile.writeFrames(inputBuffer.data(), inputBuffer.size() / channels);
		}

		// If recording is enabled, write the (raw) new samples.
		if (recorder.isOpen()) {
			recorder.writeFrames(inputBuffer.data(), inputBuffer.size() / channels);
		}

		// How many frames have we got?
		const std::size_t numFrames = inputBuffer.size() / channels;

		fillProcessingBuffer(setup, inputBuffer, channels, processingBuffer);

		// process everything
		syncChecker.processSampleBlock(processingBuffer);

		statsUpdateTime += float(numFrames) / sampleRate;
		totalTime += float(numFrames) / sampleRate;
		totalFrames += numFrames;

		// Publish statistics at the configured refresh rate.
		if (statsUpdateTime >= settings.m_statsRefreshRate) {
			snapshot.m_sequence++;
			snapshot.m_timestamp = totalTime;
			snapshot.m_frames = totalFrames;
			collectResults(setup, syncChecker, snapshot);
			if (onStats) {
				onStats(snapshot);
			}
			statsUpdateTime = 0.0f;
		}
	}

	finished = true;
	stopWatcher.join();

	DBG_INFO("Capture stopped.");
	return true;
}

bool SyncAnalyzer::analyzeBlock(const std::string &path, const SyncAnalyzerSettings &requestedSettings, uint64_t endFrame,
	SyncAnalyzerSnapshot &snapshot, std::string &error)
{
	WavReader reader;
	if (!reader.open(path)) {
		error = "cannot open " + path;
		return false;
	}
	const unsigned int channels = reader.getChannels();
	const unsigned int sampleRate = reader.getSampleRate();

	SyncAnalyzerSettings settings;
	if (!resolveSettings(requestedSettings, channels, sampleRate, settings, error)) {
		return false;
	}
	CheckerSetup setup;
	if (!setupChecker(settings, sampleRate, channels, setup, error)) {
		return false;
	}

	// Read exactly one block ending at endFrame.
	const uint64_t blockSize = settings.m_blockSize;
	const uint64_t startFrame = endFrame > blockSize ? endFrame - blockSize : 0;
	std::vector<float> input(blockSize * channels, 0.0f);
	if (!reader.seek(startFrame) || reader.readFrames(input.data(), blockSize) < blockSize) {
		error = "not enough audio for a full block at frame " + std::to_string(startFrame);
		return false;
	}

	std::vector<float> processingBuffer;
	fillProcessingBuffer(setup, input, channels, processingBuffer);

	FFTSyncCheck syncChecker(setup.m_config);
	syncChecker.setMultiChannelMode(setup.m_procChannels);
	syncChecker.setChannelAssignments(setup.m_checkerAssignments);
	syncChecker.processSampleBlock(processingBuffer);

	snapshot = SyncAnalyzerSnapshot();
	snapshot.m_sampleRate = sampleRate;
	snapshot.m_interpFactor = settings.m_interpFactor;
	snapshot.m_channels = channels;
	snapshot.m_mode = settings.m_mode;
	snapshot.m_frames = startFrame + blockSize;
	snapshot.m_timestamp = float(double(snapshot.m_frames) / sampleRate);
	collectResults(setup, syncChecker, snapshot);
	return true;
}
