#include <alsa/asoundlib.h>
#include <getopt.h>
#include <signal.h>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <set>
#include <sndfile.h>

#include "nsdkStacktrace.h"
#include "SyncCheck.h"
#include "TestSignalGenerator.h"
#include "Logger.h"
#include "AudioInput.h"
#include "AudioOutput.h"
#include "DelayStatsVisualizer.h"

static volatile bool g_running = true;

/**
 * @brief Application configuration parameters.
 */
struct AppConfig
{
	/**
	 * @brief Audio capture device.
	 */
	std::string m_captureDevice = "default";

	/**
	 * @brief Audio output device.
	 */
	std::string m_playbackDevice = "";

	/**
	 * @brief Sample rate.
	 */
	unsigned int m_sampleRate = 48000;

	/**
	 * @brief Input sample format.
	 */
	snd_pcm_format_t m_format = SND_PCM_FORMAT_S16_LE;

	/**
	 * @brief Number of physical input channels.
	 */
	unsigned int m_channels = 2;

	/**
	 * @brief Audio period size.
	 */
	snd_pcm_uframes_t m_periodSize = 4096;

	/**
	 * @brief Number of audio periods.
	 */
	unsigned int m_periods = 4;

	/**
	 * @brief FFT block size.
	 */
	unsigned int m_blockSize = SyncCheckConfig::DEFAULT_BLOCK_SIZE;

	/**
	 * @brief Overlap percent.
	 */
	unsigned int m_overlapPercent = SyncCheckConfig::DEFAULT_OVERLAP_PERCENT;

	/**
	 * @brief Number of frequency bands.
	 */
	unsigned int m_numBands = SyncCheckConfig::DEFAULT_NUM_BANDS;

	/**
	 * @brief Interpolation factor for spectral analysis.
	 */
	unsigned int m_interpFactor = SyncCheckConfig::DEFAULT_INTERP_FACTOR;

	/**
	 * @brief Start frequency of processing bands in Hz.
	 */
	unsigned int m_startFreq = 16000;

	/**
	 * @brief Frequency range of processing bands in Hz.
	 */
	unsigned int m_freqRange = 8000;

	/**
	 * @brief Band separation of processing bands in Hz.
	 */
	unsigned int m_bandSep = 200;

	/**
	 * @brief File name for output statistics dumps.
	 */
	std::string	m_dumpStatsFileName;

	/**
	 * @brief Audio dump enabled.
	 */
	bool m_dumpAudio = false;

	/**
	 * @brief Dump correlation data for debugging.
	 */
	bool m_dumpCorrelation = false;

	/**
	 * @brief Stats refresh rate in seconds.
	 */
	float m_statsRefreshRate = 1.0f;

	/**
	 * @brief Use colors.
	 */
	bool m_useColors = true;

	/**
	 * @brief Unified source mapping.
	 *
	 * Provide "mix" to mix all physical channels or a CSV list of mappings.
	 * Each mapping is either a single number (physical channel, defaults to band 0)
	 * or in the form "physical:band" (e.g. "0:0,1:1" assigns physical channels 0 and 1
	 * to band 0 and band 1 respectively).
	 */
	std::string m_sources;

	/**
	 * @brief Flag indicating whether to capture each physical channel individually.
	 * If false, capture code will mix all channels.
	 */
	bool m_multichannel = false;
};

void signalHandler(int signum)
{
	g_running = false;
}

void printUsage(const char *programName)
{
	std::cerr << "Usage: " << programName << " [options]\n"
			  << "Options:\n"
			  << "  -d, --device=NAME       ALSA device name (default: default)\n"
			  << "  -r, --rate=RATE        Sample rate (default: 48000)\n"
			  << "  -f, --format=FORMAT    Sample format (S16_LE, S32_LE, FLOAT_LE) (default: S16_LE)\n"
			  << "  -o, --output=FILE      Output statistics to file\n"
			  << "  -O, --output-device=NAME ALSA output device name (default: none)\n"
			  << "  -n, --no-colors        Disable ANSI colors in output\n"
			  << "  -c, --channels=NUM     Number of input channels (default: 1)\n"
			  << "  -p, --period-size=NUM  Period size (default: 1024)\n"
			  << "      --dump-correlation Enable correlation data dumping\n"
			  << "      --block-size=NUM   FFT block size (default: " << SyncCheckConfig::DEFAULT_BLOCK_SIZE << ")\n"
			  << "      --overlap=NUM      Overlap percent (50 or 75, default: " << SyncCheckConfig::DEFAULT_OVERLAP_PERCENT << ")\n"
			  << "      --bands=NUM        Number of frequency bands (default: " << SyncCheckConfig::DEFAULT_NUM_BANDS << ")\n"
			  << "      --interp=NUM       Interpolation factor (default: " << SyncCheckConfig::DEFAULT_INTERP_FACTOR << ")\n"
			  << "      --start-freq=NUM   Start frequency in Hz (default: 16000)\n"
			  << "      --freq-range=NUM   Frequency range in Hz (default: 8000)\n"
			  << "      --band-sep=NUM     Band separation in Hz (default: 200)\n"
			  << "      --refresh-rate=NUM Statistics refresh rate in seconds (default: 1.0)\n"
			  << "      --dump              Enable audio dump to WAV files (auto-named)\n"
			  << "  -S, --sources=MAPPINGS   Specify input mapping. Use \"mix\" to mix all channels or provide a comma-separated list (e.g. \"0:0,1:1\" or \"0,1\").\n"
			  << "  -h, --help            Show this help message\n";
}

snd_pcm_format_t parseFormat(const std::string &format)
{
	if (format == "S16_LE")
		return SND_PCM_FORMAT_S16_LE;
	if (format == "S32_LE")
		return SND_PCM_FORMAT_S32_LE;
	if (format == "FLOAT_LE")
		return SND_PCM_FORMAT_FLOAT_LE;
	return SND_PCM_FORMAT_UNKNOWN;
}

std::pair<bool, AppConfig> parseCommandLine(int argc, char *argv[])
{
	AppConfig config;

	static struct option long_options[] = {
		{ "device",			required_argument, 0, 'd' },
		{ "rate",			required_argument, 0, 'r' },
		{ "format",			required_argument, 0, 'f' },
		{ "output",			required_argument, 0, 'o' },
		{ "output-device",	required_argument, 0, 'O' },
		{ "no-colors",		no_argument,	   0, 'n' },
		{ "channels",		required_argument, 0, 'c' },
		{ "period-size",	required_argument, 0, 'p' },
		{ "block-size",		required_argument, 0, 1 },
		{ "overlap",		required_argument, 0, 2 },
		{ "bands",			required_argument, 0, 3 },
		{ "interp",			required_argument, 0, 4 },
		{ "dump",			no_argument,		0, 5 },
		{ "start-freq",		required_argument, 0, 6 },
		{ "freq-range",		required_argument, 0, 7 },
		{ "band-sep",		required_argument, 0, 8 },
		{ "refresh-rate",	required_argument, 0, 9 },
		{ "dump-correlation", no_argument,	  0, 10 },
		{ "sources",		required_argument, 0, 'S' },
		{ "help",			no_argument,	   0, 'h' },
		{ 0, 0, 0, 0 }
	};

	int opt;
	while ((opt = getopt_long(argc, argv, "d:r:f:o:O:nc:p:s:h", long_options, nullptr)) != -1) {
		switch (opt) {
		case 'd':
			config.m_captureDevice = optarg;
			break;
		case 'r':
			config.m_sampleRate = std::stoi(optarg);
			break;
		case 'f':
			config.m_format = parseFormat(optarg);
			if (config.m_format == SND_PCM_FORMAT_UNKNOWN) {
				DBG_ERROR("Invalid format specified");
				return { false, config };
			}
			break;
		case 'o':
			config.m_dumpStatsFileName = optarg;
			break;
		case 'O':
			config.m_playbackDevice = optarg;
			break;
		case 'n':
			config.m_useColors = false;
			break;
		case 'c':
			config.m_channels = std::stoi(optarg);
			break;
		case 'p':
			config.m_periodSize = std::stoi(optarg);
			break;
		case 'S': {
			config.m_sources = optarg;
			break;
		}
		case 1: // block-size
			config.m_blockSize = std::stoi(optarg);
			break;
		case 2: // overlap
			config.m_overlapPercent = std::stoi(optarg);
			if (config.m_overlapPercent != 50 && config.m_overlapPercent != 75) {
				DBG_ERROR("Invalid overlap percent. Must be 50 or 75");
				return { false, config };
			}
			break;
		case 3: // bands
			config.m_numBands = std::stoi(optarg);
			break;
		case 4: // interp
			config.m_interpFactor = std::stoi(optarg);
			break;
		case 5: // dump
			config.m_dumpAudio = true;
			break;
		case 6: // start-freq
			config.m_startFreq = std::stoi(optarg);
			break;
		case 7: // freq-range
			config.m_freqRange = std::stoi(optarg);
			break;
		case 8: // band-sep
			config.m_bandSep = std::stoi(optarg);
			break;
		case 9: // refresh-rate
			config.m_statsRefreshRate = std::stof(optarg);
			if (config.m_statsRefreshRate <= 0.0f) {
				DBG_ERROR("Invalid refresh rate. Must be greater than 0");
				return { false, config };
			}
			break;
		case 10:
			config.m_dumpCorrelation = true;
			break;
		case 'h':
			return { false, config };
		default:
			return { false, config };
		}
	}

	return { true, config };
}

int main(int argc, char *argv[])
{
	// Install stack trace handler
	nsdkStacktrace::install();

	// Set desired log level
	Logger::getInstance().setLogLevel(LOG_DEBUG);

	// Set up signal handling
	signal(SIGINT, signalHandler);
	signal(SIGTERM, signalHandler);

	if (argc < 2) {
		printUsage(argv[0]);
		return 1;
	}

	auto [configValid, config] = parseCommandLine(argc, argv);
	if (!configValid) {
		printUsage(argv[0]);
		return 1;
	}

	// If no mapping is specified, default to mix mode.
	if (config.m_sources.empty()) {
		config.m_sources = "mix";
	}

	// Set multichannel flag based on the mapping.
	config.m_multichannel = (config.m_sources != "mix");

	// Initialize capture device using individual settings.
	// (Assuming config.m_captureDevice still holds the device string.)
	std::unique_ptr<AudioSource> capture;
	if (config.m_captureDevice.find("file://") == 0) {
		capture = std::make_unique<FileCapture>(config.m_captureDevice, config.m_periodSize);
	} else {
		capture = std::make_unique<AlsaCapture>(config.m_captureDevice,
			config.m_channels,
			config.m_format,
			config.m_sampleRate,
			config.m_periodSize,
			config.m_periods);
	}

	// After capture initialization, update sampleRate and channels accordingly.
	config.m_sampleRate = capture->getSampleRate();
	config.m_channels   = capture->getChannels();

	// Initialize playback device if requested.
	// (AlsaPlayback has been updated similarly to use individual output parameters.)
	std::unique_ptr<AlsaPlayback> playback;
	if (!config.m_playbackDevice.empty()) {
		playback = std::make_unique<AlsaPlayback>(config.m_playbackDevice,
			config.m_channels,
			config.m_format,
			config.m_sampleRate,
			config.m_periodSize,
			config.m_periods);
		if (!playback->isInitialized()) {
			DBG_ERROR("Failed to initialize audio output device: " << config.m_playbackDevice);
			return 1;
		}
		DBG_INFO("Audio output enabled on device: " << config.m_playbackDevice);
	}

	// Initialize visualizer
	DelayStatsVisualizer visualizer(config.m_dumpStatsFileName, config.m_useColors);

	DBG_INFO("Starting capture from device: " << config.m_captureDevice);
	DBG_INFO("Sample rate: " << config.m_sampleRate);
	DBG_INFO("Physical channels: " << config.m_channels);
	DBG_INFO("Block size: " << config.m_blockSize);
	DBG_INFO("Overlap: " << config.m_overlapPercent << "%");
	DBG_INFO("Number of bands: " << config.m_numBands);
	DBG_INFO("Interpolation factor: " << config.m_interpFactor);
	DBG_INFO("Start frequency: " << config.m_startFreq);
	DBG_INFO("Frequency range: " << config.m_freqRange);
	DBG_INFO("Band separation: " << config.m_bandSep);
	DBG_INFO("Sources mapping: " << config.m_sources);

	// In mix mode we process a single mono channel.
	// Otherwise, we will build a custom channel assignment based on --sources.
	SyncCheckConfig syncConfig(config.m_blockSize, config.m_overlapPercent, config.m_sampleRate, config.m_numBands, config.m_interpFactor);
	syncConfig.initBands(config.m_startFreq, config.m_freqRange, config.m_bandSep);
	syncConfig.setCorrelationDumpEnabled(config.m_dumpCorrelation);

	// Initialize FFTSyncCheck.
	FFTSyncCheck syncChecker(syncConfig);

	// Setup channel assignments.
	std::vector<FFTSyncCheck::ChannelAssignment> assignments;
	if (config.m_sources == "mix")
	{
		// Create default channel assignments for mix mode.
		// In mix mode, there is a single input channel (channel 0).
		// We assign each band (except the reference band, assumed to be band 0) as its own processing assignment.
		for (std::size_t band = 1; band < config.m_numBands; band++) {
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
		std::istringstream srcStream(config.m_sources);
		std::string token;
		int refChannel = -1;
		int refBand = -1;
		while (std::getline(srcStream, token, ',')) {
			if (token.empty())
				continue;
			size_t pos = token.find(':');
			int ph = 0, band = 0;
			if (pos != std::string::npos) {
				ph = std::stoi(token.substr(0, pos));
				band = std::stoi(token.substr(pos + 1));
			} else {
				ph = std::stoi(token);
				band = 0;
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
		}
		syncChecker.setMultiChannelMode(assignments.size());
		syncChecker.setChannelAssignments(assignments);
	}

	// Allocate processing buffer.
	// For mixing mode, we expect blockSize samples.
	// For per‑channel processing, we need blockSize frames × (number of assignments).
	unsigned int procChannels = (config.m_sources == "mix") ? 1 : (unsigned int)assignments.size();
	std::vector<float> processingBuffer(config.m_blockSize * procChannels, 0.0f);

	float statsUpdateTime = 0.0f;
	float totalTime = 0.0f;

	// Initialize WAV dump file if requested
	SNDFILE* dumpFile = nullptr;
	SF_INFO sfInfo = {};
	if (config.m_dumpAudio) {
		// Create filename with configuration parameters
		std::stringstream ss;
		ss << "dump_"
		   << config.m_startFreq << "hz_"
		   << config.m_freqRange << "range_"
		   << config.m_bandSep << "sep_"
		   << config.m_numBands << "bands.wav";
		std::string dumpFileName = ss.str();

		sfInfo.samplerate = config.m_sampleRate;
		sfInfo.channels = config.m_channels;
		sfInfo.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

		dumpFile = sf_open(dumpFileName.c_str(), SFM_WRITE, &sfInfo);
		if (!dumpFile) {
			DBG_ERROR("Failed to open dump file: " << dumpFileName << " (" << sf_strerror(nullptr) << ")");
			return 1;
		}
		DBG_INFO("Dumping audio to: " << dumpFileName);
	}

	// If playback is enabled, kick it off with writing a full block of zeros.
	if (playback) {
		std::vector<float> zeroBlock(config.m_blockSize * procChannels, 0.0f);
		playback->playBlock(zeroBlock);
	}

	while (g_running) {
		std::vector<float> inputBuffer = capture->captureBlock();
		if (inputBuffer.empty()) {
			DBG_ERROR("Capture returned empty buffer. Processing stopped.");
			g_running = false;
			break;
		}

		// If playback is enabled, write the captured block to the audio output.
		if (playback) {
			playback->playBlock(inputBuffer);
		}

		// If dumping is enabled, write the (raw) new samples.
		if (dumpFile) {
			sf_writef_float(dumpFile, inputBuffer.data(), inputBuffer.size() / config.m_channels);
		}

		// How many frames have we got?
		const std::size_t numFrames = inputBuffer.size() / config.m_channels;

		if (config.m_multichannel) {
			// In non-mix mode, inputBuffer is interleaved over config.channels.
			processingBuffer.resize(numFrames * config.m_channels);
			// For each frame, extract only the channels needed as defined by assignments.
			for (unsigned int i = 0; i < numFrames; i++) {
				for (unsigned int m = 0; m < assignments.size(); m++) {
					// Use the physical signal channel defined in the assignment.
					int ph = assignments[m].sigChannel;
					processingBuffer[(i) * procChannels + m] = inputBuffer[i * config.m_channels + ph];
				}
			}
		}
		else {
			// Mix mode. Mix all channels together.
			processingBuffer.resize(numFrames);
			std::fill(processingBuffer.begin(), processingBuffer.end(), 0.0f);
			for (unsigned int j = 0; j < config.m_channels; j++) {
				for (unsigned int i = 0; i < numFrames; i++) {
					processingBuffer[i] += inputBuffer[i * config.m_channels + j] / config.m_channels;
				}
			}
		}

		// process everything
		syncChecker.processSampleBlock(processingBuffer);

		statsUpdateTime += float(numFrames) / config.m_sampleRate;
		totalTime += float(numFrames) / config.m_sampleRate;

		// Update statistics visualization at the configured refresh rate.
		if (statsUpdateTime >= config.m_statsRefreshRate) {
			// getDelayStats now returns a vector of tuples:
			// (ChannelAssignment, filtered delay, confidence, filtered PSLR)
			std::vector<std::tuple<FFTSyncCheck::ChannelAssignment, float, float, float>> estimates = syncChecker.getDelayStats();
			visualizer.visualizeDelays(estimates, totalTime);
			statsUpdateTime = 0.0f;
		}
	}

	// Cleanup
	if (dumpFile) {
		sf_close(dumpFile);
	}

	DBG_INFO("Capture stopped.");
	return 0;
}