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
	std::string			captureDevice = "default";
	unsigned int		sampleRate = 48000;
	snd_pcm_format_t 	format = SND_PCM_FORMAT_S16_LE;
	std::string			outputFile;
	bool				useColors = true;
	unsigned int		channels = 2;
	snd_pcm_uframes_t	periodSize = 4096;
	unsigned int		periods = 4;
	unsigned int		blockSize = SyncCheckConfig::DEFAULT_BLOCK_SIZE;
	unsigned int		overlapPercent = SyncCheckConfig::DEFAULT_OVERLAP_PERCENT;
	unsigned int		numBands = SyncCheckConfig::DEFAULT_NUM_BANDS;
	unsigned int		interpFactor = SyncCheckConfig::DEFAULT_INTERP_FACTOR;
	unsigned int		startFreq = 16000;
	unsigned int		freqRange = 8000;
	unsigned int		bandSep = 200;
	bool				dumpEnabled = false;
	float				statsRefreshRate = 1.0f;  // Stats refresh rate in seconds
	bool				dumpCorrelation = false;

	/**
	 * @brief Unified source mapping.
	 *
	 * Provide "mix" to mix all physical channels or a CSV list of mappings.
	 * Each mapping is either a single number (physical channel, defaults to band 0)
	 * or in the form "physical:band" (e.g. "0:0,1:1" assigns physical channels 0 and 1
	 * to band 0 and band 1 respectively).
	 */
	std::string sources;

	/**
	 * @brief Flag indicating whether to capture each physical channel individually.
	 * If false, capture code will mix all channels.
	 */
	bool multichannel = false;

	/**
	 * @brief ALSA output device.
	 */
	std::string outputDevice;
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
			config.captureDevice = optarg;
			break;
		case 'r':
			config.sampleRate = std::stoi(optarg);
			break;
		case 'f':
			config.format = parseFormat(optarg);
			if (config.format == SND_PCM_FORMAT_UNKNOWN) {
				DBG_ERROR("Invalid format specified");
				return { false, config };
			}
			break;
		case 'o':
			config.outputFile = optarg;
			break;
		case 'O':
			config.outputDevice = optarg;
			break;
		case 'n':
			config.useColors = false;
			break;
		case 'c':
			config.channels = std::stoi(optarg);
			break;
		case 'p':
			config.periodSize = std::stoi(optarg);
			break;
		case 'S': {
			config.sources = optarg;
			break;
		}
		case 1: // block-size
			config.blockSize = std::stoi(optarg);
			break;
		case 2: // overlap
			config.overlapPercent = std::stoi(optarg);
			if (config.overlapPercent != 50 && config.overlapPercent != 75) {
				DBG_ERROR("Invalid overlap percent. Must be 50 or 75");
				return { false, config };
			}
			break;
		case 3: // bands
			config.numBands = std::stoi(optarg);
			break;
		case 4: // interp
			config.interpFactor = std::stoi(optarg);
			break;
		case 5: // dump
			config.dumpEnabled = true;
			break;
		case 6: // start-freq
			config.startFreq = std::stoi(optarg);
			break;
		case 7: // freq-range
			config.freqRange = std::stoi(optarg);
			break;
		case 8: // band-sep
			config.bandSep = std::stoi(optarg);
			break;
		case 9: // refresh-rate
			config.statsRefreshRate = std::stof(optarg);
			if (config.statsRefreshRate <= 0.0f) {
				DBG_ERROR("Invalid refresh rate. Must be greater than 0");
				return { false, config };
			}
			break;
		case 10:
			config.dumpCorrelation = true;
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
	if (config.sources.empty())
	{
		config.sources = "mix";
	}

	// Set multichannel flag based on the mapping.
	config.multichannel = (config.sources != "mix");

	// Initialize capture device using individual settings.
	// (Assuming config.captureDevice still holds the device string.)
	std::unique_ptr<AudioSource> capture;
	if (config.captureDevice.find("file://") == 0) {
		capture = std::make_unique<FileCapture>(config.captureDevice, config.periodSize, config.multichannel);
	} else {
		capture = std::make_unique<AlsaCapture>(config.captureDevice,
			config.channels,
			config.format,
			config.sampleRate,
			config.periodSize,
			config.periods,
			config.multichannel);
	}

	// After capture initialization, update sampleRate and channels accordingly.
	config.sampleRate = capture->getSampleRate();
	config.channels   = capture->getChannels();

	// Initialize playback device if requested.
	// (AlsaPlayback has been updated similarly to use individual output parameters.)
	std::unique_ptr<AlsaPlayback> playback;
	if (!config.outputDevice.empty()) {
		playback = std::make_unique<AlsaPlayback>(config.outputDevice,
			config.channels,
			config.format,
			config.sampleRate,
			config.periodSize,
			config.periods);
		if (!playback->isInitialized()) {
			DBG_ERROR("Failed to initialize audio output device: " << config.outputDevice);
			return 1;
		}
		DBG_INFO("Audio output enabled on device: " << config.outputDevice);
	}

	// Initialize visualizer
	DelayStatsVisualizer visualizer(config.outputFile, config.useColors);

	DBG_INFO("Starting capture from device: " << config.captureDevice);
	DBG_INFO("Sample rate: " << config.sampleRate);
	DBG_INFO("Physical channels: " << config.channels);
	DBG_INFO("Block size: " << config.blockSize);
	DBG_INFO("Overlap: " << config.overlapPercent << "%");
	DBG_INFO("Number of bands: " << config.numBands);
	DBG_INFO("Interpolation factor: " << config.interpFactor);
	DBG_INFO("Start frequency: " << config.startFreq);
	DBG_INFO("Frequency range: " << config.freqRange);
	DBG_INFO("Band separation: " << config.bandSep);
	DBG_INFO("Sources mapping: " << config.sources);

	// In mix mode we process a single mono channel.
	// Otherwise, we will build a custom channel assignment based on --sources.
	SyncCheckConfig syncConfig(config.blockSize, config.overlapPercent, config.sampleRate, config.numBands, config.interpFactor);
	syncConfig.initBands(config.startFreq, config.freqRange, config.bandSep);
	syncConfig.setCorrelationDumpEnabled(config.dumpCorrelation);

	// Initialize FFTSyncCheck.
	FFTSyncCheck syncChecker(syncConfig);

	// Setup channel assignments.
	std::vector<FFTSyncCheck::ChannelAssignment> assignments;
	if (config.sources == "mix")
	{
		// Create default channel assignments for mix mode.
		// In mix mode, there is a single input channel (channel 0).
		// We assign each band (except the reference band, assumed to be band 0) as its own processing assignment.
		for (std::size_t band = 1; band < config.numBands; band++) {
			FFTSyncCheck::ChannelAssignment assign;
			assign.refChannel = 0;
			assign.sigChannel = 0;
			assign.refBand = band;
			assign.sigBand = 0;
			assignments.push_back(assign);
		}
		syncChecker.setMultiChannelMode(1);
		syncChecker.setChannelAssignments(assignments);
	}
	else
	{
		std::istringstream srcStream(config.sources);
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
	unsigned int procChannels = (config.sources == "mix") ? 1 : (unsigned int)assignments.size();
	std::vector<float> processingBuffer(config.blockSize * procChannels, 0.0f);
	// 'framesFilled' counts how many complete frames have been copied.
	size_t framesFilled = 0;
	float statsUpdateTime = 0.0f;

	// Initialize WAV dump file if requested
	SNDFILE* dumpFile = nullptr;
	SF_INFO sfInfo = {};
	if (config.dumpEnabled) {
		// Create filename with configuration parameters
		std::stringstream ss;
		ss << "dump_"
		   << config.startFreq << "hz_"
		   << config.freqRange << "range_"
		   << config.bandSep << "sep_"
		   << config.numBands << "bands.wav";
		std::string dumpFileName = ss.str();

		sfInfo.samplerate = config.sampleRate;
		sfInfo.channels = procChannels;
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
		std::vector<float> newSamples = capture->captureBlock();
		if (newSamples.empty()) {
			DBG_ERROR("Capture returned empty buffer. Processing stopped.");
			g_running = false;
			break;
		}

		// If playback is enabled, write the captured block to the audio output.
		if (playback) {
			playback->playBlock(newSamples);
		}

		if (config.multichannel) {
			// In non-mix mode, newSamples is interleaved over config.channels.
			unsigned int newFrames = newSamples.size() / config.channels;
			unsigned int framesNeeded = config.blockSize - framesFilled;
			unsigned int framesToCopy = std::min(newFrames, framesNeeded);
			// For each frame, extract only the channels needed as defined by assignments.
			for (unsigned int i = 0; i < framesToCopy; i++) {
				for (unsigned int m = 0; m < assignments.size(); m++) {
					// Use the physical signal channel defined in the assignment.
					int ph = assignments[m].sigChannel;
					processingBuffer[(framesFilled + i) * procChannels + m] = newSamples[i * config.channels + ph];
				}
			}
			framesFilled += framesToCopy;
		}
		else {
			// Mix mode: capture code already returns mixed samples.
			size_t samplesToProcess = std::min(newSamples.size(), config.blockSize - framesFilled);
			std::copy(newSamples.begin(), newSamples.begin() + samplesToProcess, processingBuffer.begin() + framesFilled);
			framesFilled += samplesToProcess;
		}

		// Process once we have a full block.
		if (framesFilled >= config.blockSize) {
			syncChecker.processSampleBlock(processingBuffer);

			// Calculate hop size based on overlap (in frames) and shift processed data.
			size_t overlapFrames = (config.blockSize * config.overlapPercent) / 100;
			size_t hopFrames = config.blockSize - overlapFrames;
			if (overlapFrames > 0) {
				if (config.multichannel) {
					std::copy(processingBuffer.begin() + hopFrames * procChannels, processingBuffer.begin() + config.blockSize * procChannels,
							  processingBuffer.begin());
				}
				else {
					std::copy(processingBuffer.begin() + hopFrames, processingBuffer.begin() + config.blockSize, processingBuffer.begin());
				}
				framesFilled = overlapFrames;
			}
			else {
				framesFilled = 0;
			}

			statsUpdateTime += float(hopFrames) / config.sampleRate;

			// Update statistics visualization at the configured refresh rate.
			if (statsUpdateTime >= config.statsRefreshRate) {
				// getDelayStats now returns a vector of tuples:
				// (ChannelAssignment, filtered delay, confidence, filtered PSLR)
				std::vector<std::tuple<FFTSyncCheck::ChannelAssignment, float, float, float>> estimates = syncChecker.getDelayStats();
				visualizer.visualizeDelays(estimates);
				statsUpdateTime = 0.0f;
			}
		}

		// If dumping is enabled, write the (raw) new samples.
		if (dumpFile) {
			sf_writef_float(dumpFile, newSamples.data(), newSamples.size() / procChannels);
		}
	}

	// Cleanup
	if (dumpFile) {
		sf_close(dumpFile);
	}

	DBG_INFO("Capture stopped.");
	return 0;
}