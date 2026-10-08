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

bool SyncAnalyzer::run(const SyncAnalyzerSettings &settings, const StatsCallback &onStats, std::string &error)
{
	m_stopRequested = false;

	// Initialize capture device.
	std::unique_ptr<AudioSource> capture;
	if (settings.m_captureDevice.find("file://") == 0) {
		capture = std::make_unique<FileCapture>(settings.m_captureDevice, settings.m_periodSize, settings.m_realtimeFile);
	} else {
		capture = std::make_unique<DeviceCapture>(settings.m_captureDevice,
			settings.m_channels,
			settings.m_sampleRate,
			settings.m_periodSize,
			settings.m_periods);
	}
	if (!capture->isInitialized()) {
		error = "failed to open capture device " + settings.m_captureDevice;
		return false;
	}

	// After capture initialization, take sample rate and channels from the actual source.
	const unsigned int sampleRate = capture->getSampleRate();
	const unsigned int channels = capture->getChannels();

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

	const bool multichannel = (settings.m_sources != "mix");

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

	// In mix mode we process a single mono channel.
	// Otherwise, we will build a custom channel assignment based on the source mapping.
	SyncCheckConfig syncConfig(settings.m_blockSize, settings.m_overlapPercent, sampleRate, settings.m_numBands, settings.m_interpFactor);
	syncConfig.initBands(settings.m_startFreq, settings.m_freqRange, settings.m_bandSep);
	syncConfig.setCorrelationDumpEnabled(settings.m_dumpCorrelation);

	// Initialize FFTSyncCheck.
	FFTSyncCheck syncChecker(syncConfig);

	// Setup channel assignments.
	std::vector<FFTSyncCheck::ChannelAssignment> assignments;
	if (!multichannel)
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
		syncChecker.setMultiChannelMode(1);
		syncChecker.setChannelAssignments(assignments);
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
		syncChecker.setMultiChannelMode(assignments.size());
		syncChecker.setChannelAssignments(assignments);
	}

	// Allocate processing buffer.
	// For mixing mode, we expect blockSize samples.
	// For per-channel processing, we need blockSize frames x (number of assignments).
	unsigned int procChannels = multichannel ? (unsigned int)assignments.size() : 1;
	std::vector<float> processingBuffer(settings.m_blockSize * procChannels, 0.0f);

	float statsUpdateTime = 0.0f;
	float totalTime = 0.0f;
	SyncAnalyzerSnapshot snapshot;
	snapshot.m_sampleRate = sampleRate;
	snapshot.m_interpFactor = settings.m_interpFactor;

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

		// How many frames have we got?
		const std::size_t numFrames = inputBuffer.size() / channels;

		if (multichannel) {
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

		// process everything
		syncChecker.processSampleBlock(processingBuffer);

		statsUpdateTime += float(numFrames) / sampleRate;
		totalTime += float(numFrames) / sampleRate;

		// Publish statistics at the configured refresh rate.
		if (statsUpdateTime >= settings.m_statsRefreshRate) {
			snapshot.m_sequence++;
			snapshot.m_timestamp = totalTime;
			snapshot.m_results.clear();
			for (const auto &report : syncChecker.getDelayStats()) {
				const auto &assign = report.assignment;
				// Skip self-assignments
				if (assign.refChannel == assign.sigChannel && assign.refBand == assign.sigBand) {
					continue;
				}
				snapshot.m_results.push_back({ report, syncChecker.getCorrelationData(assign) });
			}
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
