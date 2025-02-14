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

// ANSI color codes
#define ANSI_COLOR_RED "\x1b[31m"
#define ANSI_COLOR_GREEN "\x1b[32m"
#define ANSI_COLOR_YELLOW "\x1b[33m"
#define ANSI_COLOR_BLUE "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN "\x1b[36m"
#define ANSI_COLOR_RESET "\x1b[0m"

static volatile bool g_running = true;

struct AppConfig
{
#ifdef BUILDTYPE_PC
	std::string captureDevice = "file://untitled.wav";
	// std::string captureDevice = "file://test_signal_mixed.wav";
#else
	std::string captureDevice = "default";
#endif
	unsigned int sampleRate = 48000;
	snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;
	std::string outputFile;
	bool useColors = true;
	unsigned int channels = 1;
	unsigned int periodSize = 1024;
	unsigned int periods = 2;
	unsigned int blockSize = SyncCheckConfig::DEFAULT_BLOCK_SIZE;
	unsigned int overlapPercent = SyncCheckConfig::DEFAULT_OVERLAP_PERCENT;
	unsigned int numBands = SyncCheckConfig::DEFAULT_NUM_BANDS;
	unsigned int interpFactor = SyncCheckConfig::DEFAULT_INTERP_FACTOR;
	unsigned int startFreq = 14000;
	unsigned int freqRange = 8000;
	unsigned int bandSep = 500;
	int processChannel = -1;
	std::string dumpFile;
	float statsRefreshRate = 1.0f;  // Stats refresh rate in seconds
	std::set<int> bandsOfInterest;  // Bands to include in reports (empty = all bands)
};

void signalHandler(int signum)
{
	g_running = false;
}

class DelayStatsVisualizer
{
private:
	std::ofstream m_outputFile;
	bool m_useColors;
	unsigned int m_dumpId;
	float m_maxDelay;		// Maximum delay range (+/-)
	unsigned int m_width;	// Width of ASCII visualization

public:
	DelayStatsVisualizer(const std::string &outputFile, bool useColors, float maxDelay = 5.0f, unsigned int width = 40)
		: m_useColors(useColors)
		, m_dumpId(0)
		, m_maxDelay(maxDelay)
		, m_width(width)
	{
		if (!outputFile.empty()) {
			m_outputFile.open(outputFile);
		}
	}

	void visualizeDelays(const std::vector<std::pair<float, float>> &estimates, const std::set<int> &bandsOfInterest)
	{
		std::stringstream ss;
		ss << std::fixed << std::setprecision(3);
		ss << "Delays (ms):\n";

		// Calculate center position
		int center = m_width / 2;

		for (size_t i = 0; i < estimates.size(); ++i) {
			double delay = estimates[i].first;
			double confidence = estimates[i].second;

			std::string color;
			if (m_useColors) {
				if (confidence > 0.7f)
					color = ANSI_COLOR_GREEN;
				else if (confidence > 0.25f)
					color = ANSI_COLOR_YELLOW;
				else
					color = ANSI_COLOR_RED;
			}

			// Numeric display
			if (bandsOfInterest.empty() || bandsOfInterest.find(i) != bandsOfInterest.end()) {
				ss << (m_useColors ? color : "") << "Band " << (i + 1) << ": "
				<< std::setw(8) << delay << " ms [" << confidence << "]"
				<< (m_useColors ? ANSI_COLOR_RESET : "") << " |";

				// ASCII visualization
				int position = center + static_cast<int>((delay / m_maxDelay) * (center - 1));
				position = std::max(0, std::min(static_cast<int>(m_width - 1), position));

				for (unsigned int j = 0; j < m_width; ++j) {
					if (j == position) {
						ss << (m_useColors ? color : "") << "*" << (m_useColors ? ANSI_COLOR_RESET : "");
					} else if (j == center) {
						ss << "|";  // Center mark
					} else {
						ss << "-";
					}
				}
				ss << "|\n";
			}
		}
		ss << "\nScale: each division = " << (m_maxDelay / (center - 1)) << " ms\n\n";

		// Output to console
		std::cout << ss.str() << std::flush;

		// Output to file if opened
		if (m_outputFile.is_open()) {
			m_outputFile << std::setw(6) << std::setfill(' ') << m_dumpId++ << ", ";
			for (size_t i = 0; i < estimates.size(); ++i) {

				if (bandsOfInterest.empty() || bandsOfInterest.find(i) != bandsOfInterest.end()) {
					const double delay = estimates[i].first;
					const double confidence = estimates[i].second;

					m_outputFile << "Band " << std::setw(1) << (i + 1) << ": "
						<< std::fixed << std::setprecision(3) << std::setw(8) << delay
						<< " ms, confidence: " << std::setw(5) << confidence << "";
					if (i < estimates.size() - 1)
						m_outputFile << ",";
				}
			}
			m_outputFile << std::endl;
		}
	}
};

void printUsage(const char *programName)
{
	std::cerr << "Usage: " << programName << " [options]\n"
			  << "Options:\n"
			  << "  -d, --device=NAME       ALSA device name (default: default)\n"
			  << "  -r, --rate=RATE        Sample rate (default: 48000)\n"
			  << "  -f, --format=FORMAT    Sample format (S16_LE, S32_LE, FLOAT_LE) (default: S16_LE)\n"
			  << "  -o, --output=FILE      Output statistics to file\n"
			  << "  -n, --no-colors        Disable ANSI colors in output\n"
			  << "  -c, --channels=NUM     Number of input channels (default: 1)\n"
			  << "  -s  --process-ch=NUM   Channel to process (0-based, default: 0)\n"
			  << "  -p, --period-size=NUM  Period size (default: 1024)\n"
			  << "      --block-size=NUM   FFT block size (default: 8192)\n"
			  << "      --overlap=NUM      Overlap percent (50 or 75, default: 50)\n"
			  << "      --bands=NUM        Number of frequency bands (default: 8)\n"
			  << "      --interp=NUM       Interpolation factor (default: 1)\n"
			  << "      --start-freq=NUM   Start frequency in Hz (default: 18000)\n"
			  << "      --freq-range=NUM   Frequency range in Hz (default: 4000)\n"
			  << "      --band-sep=NUM     Band separation in Hz (default: 100)\n"
			  << "      --bands-of-interest=LIST  Comma-separated list of band indices to report (default: all)\n"
			  << "      --refresh-rate=NUM Statistics refresh rate in seconds (default: 1.0)\n"
			  << "      --dump=FILE        Dump captured audio to WAV file\n"
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
		{ "device", required_argument, 0, 'd' },
		{ "rate", required_argument, 0, 'r' },
		{ "format", required_argument, 0, 'f' },
		{ "output", required_argument, 0, 'o' },
		{ "no-colors", no_argument, 0, 'n' },
		{ "channels", required_argument, 0, 'c' },
		{ "process-ch", required_argument, 0, 's' },
		{ "period-size", required_argument, 0, 'p' },
		{ "block-size", required_argument, 0, 1 },
		{ "overlap", required_argument, 0, 2 },
		{ "bands", required_argument, 0, 3 },
		{ "interp", required_argument, 0, 4 },
		{ "dump", required_argument, 0, 5 },
		{ "start-freq", required_argument, 0, 6 },
		{ "freq-range", required_argument, 0, 7 },
		{ "band-sep", required_argument, 0, 8 },
		{ "refresh-rate", required_argument, 0, 9 },
		{ "bands-of-interest", required_argument, 0, 10 },
		{ "help", no_argument, 0, 'h' },
		{ 0, 0, 0, 0 }
	};

	int opt;
	while ((opt = getopt_long(argc, argv, "d:r:f:o:nc:p:s:h", long_options, nullptr)) != -1) {
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
		case 'n':
			config.useColors = false;
			break;
		case 'c':
			config.channels = std::stoi(optarg);
			break;
		case 'p':
			config.periodSize = std::stoi(optarg);
			break;
		case 's': // process-ch
			config.processChannel = std::stoi(optarg);
			break;
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
			config.dumpFile = optarg;
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
		case 10: // bands-of-interest
			{
				std::string bands_str(optarg);
				std::stringstream ss(bands_str);
				std::string band;
				while (std::getline(ss, band, ',')) {
					int band_idx = std::stoi(band);
					if (band_idx < 1) {
						DBG_ERROR("Invalid band index " << band_idx << ". Must be in range from 1 to " << config.numBands);
						return { false, config };
					}
					config.bandsOfInterest.insert(band_idx - 1);
				}
			}
			break;
		case 'h':
			return { false, config };
		default:
			return { false, config };
		}
	}

	// Validate process channel
	if (config.processChannel >= (int)config.channels) {
		DBG_ERROR("Invalid process channel. Must be less than number of channels");
		return { false, config };
	}

	return { true, config };
}

class AudioSource {
public:
	virtual ~AudioSource() = default;
	virtual bool isInitialized() const = 0;
	virtual std::vector<float> captureBlock() = 0;
	virtual unsigned int getSampleRate() const = 0;
	virtual unsigned int getChannels() const = 0;
};

class FileCapture : public AudioSource {
private:
	AppConfig m_config;
	SNDFILE* m_file;
	SF_INFO m_sfInfo;
	std::vector<float> m_buffer;
	bool m_initialized;

public:
	FileCapture(const AppConfig& config)
		: m_config(config)
		, m_file(nullptr)
		, m_initialized(false)
	{
		std::string filepath = config.captureDevice.substr(7); // Remove "file://" prefix

		memset(&m_sfInfo, 0, sizeof(m_sfInfo));
		m_file = sf_open(filepath.c_str(), SFM_READ, &m_sfInfo);

		if (!m_file) {
			DBG_ERROR("Cannot open audio file: " << filepath << " (" << sf_strerror(nullptr) << ")");
			return;
		}

		// Override config parameters with file properties
		m_config.channels = m_sfInfo.channels;
		m_config.sampleRate = m_sfInfo.samplerate;

		if (m_config.processChannel >= (int)m_config.channels) {
			DBG_ERROR("Requested channel " << m_config.processChannel << " exceeds number of channels in file (" << m_config.channels << ")");
			sf_close(m_file);
			return;
		}

		m_buffer.resize(m_config.periodSize * m_config.channels);
		m_initialized = true;

		DBG_INFO("Opened audio file: " << filepath);
		DBG_INFO("Sample rate: " << m_sfInfo.samplerate << " Hz");
		DBG_INFO("Channels: " << m_sfInfo.channels);
		DBG_INFO("Format: 0x" << std::hex << m_sfInfo.format << std::dec);
	}

	~FileCapture() {
		if (m_file) {
			sf_close(m_file);
		}
	}

	bool isInitialized() const override {
		return m_initialized;
	}

	std::vector<float> captureBlock() override {
		std::vector<float> output(m_config.periodSize);

		if (!isInitialized()) {
			return output;
		}

		sf_count_t framesRead = sf_readf_float(m_file, m_buffer.data(), m_config.periodSize);

		if (framesRead == 0) {
			g_running = false; // End of file
			return output;
		}

		if (m_config.processChannel < 0) {
			// Mix all channels
			for (sf_count_t i = 0; i < framesRead; ++i) {
				for (unsigned int ch = 0; ch < m_config.channels; ++ch) {
					output[i] += m_buffer[i * m_config.channels + ch];
				}
			}
		} else {
			// Extract the requested channel
			for (sf_count_t i = 0; i < framesRead; ++i) {
				output[i] = m_buffer[i * m_config.channels + m_config.processChannel];
			}
		}

		// Zero-pad if we read less than requested
		if (framesRead < m_config.periodSize) {
			std::fill(output.begin() + framesRead, output.end(), 0.0f);
		}

		return output;
	}

	unsigned int getSampleRate() const override
	{
		return m_sfInfo.samplerate;
	}

	unsigned int getChannels() const override
	{
		return m_config.channels;
	}
};

class AlsaCapture : public AudioSource
{
private:
	AppConfig m_config;
	snd_pcm_t *m_handle;
	std::vector<uint8_t> m_buffer;
	unsigned int m_frameSize;

public:
	AlsaCapture(const AppConfig &config)
		: m_config(config)
		, m_handle(nullptr)
		, m_frameSize(0)
	{
		int err;
		if ((err = snd_pcm_open(&m_handle, config.captureDevice.c_str(), SND_PCM_STREAM_CAPTURE, 0)) < 0) {
			DBG_ERROR("Cannot open audio device: " << std::string(snd_strerror(err)));
			return;
		}

		snd_pcm_hw_params_t *hw_params;
		snd_pcm_hw_params_alloca(&hw_params);

		if ((err = snd_pcm_hw_params_any(m_handle, hw_params)) < 0) {
			DBG_ERROR("Cannot initialize hw params: " << std::string(snd_strerror(err)));
			return;
		}

		if ((err = snd_pcm_hw_params_set_access(m_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
			DBG_ERROR("Cannot set access type: " << std::string(snd_strerror(err)));
			return;
		}

		if ((err = snd_pcm_hw_params_set_format(m_handle, hw_params, config.format)) < 0) {
			DBG_ERROR("Cannot set sample format: " << std::string(snd_strerror(err)));
			return;
		}

		if ((err = snd_pcm_hw_params_set_channels(m_handle, hw_params, config.channels)) < 0) {
			DBG_ERROR("Cannot set channel count: " << std::string(snd_strerror(err)));
			return;
		}

		unsigned int rate = config.sampleRate;
		if ((err = snd_pcm_hw_params_set_rate_near(m_handle, hw_params, &rate, 0)) < 0) {
			DBG_ERROR("Cannot set sample rate: " << std::string(snd_strerror(err)));
			return;
		}

		// keep actual sample rate
		m_config.sampleRate = rate;

		if ((err = snd_pcm_hw_params_set_period_size(m_handle, hw_params, config.periodSize, 0)) < 0) {
			DBG_ERROR("Cannot set period size: " << std::string(snd_strerror(err)));
			return;
		}

		if ((err = snd_pcm_hw_params_set_periods(m_handle, hw_params, config.periods, 0)) < 0) {
			DBG_ERROR("Cannot set periods: " << std::string(snd_strerror(err)));
			return;
		}

		if ((err = snd_pcm_hw_params(m_handle, hw_params)) < 0) {
			DBG_ERROR("Cannot set parameters: " << std::string(snd_strerror(err)));
			return;
		}

		// Calculate frame size for all channels
		m_frameSize = snd_pcm_format_width(config.format) / 8 * config.channels;
		// Allocate buffer for all channels
		m_buffer.resize(config.periodSize * m_frameSize);
	}

	~AlsaCapture()
	{
		if (m_handle) {
			snd_pcm_close(m_handle);
		}
	}

	bool isInitialized() const override
	{
		return m_handle != nullptr && m_frameSize > 0;
	}

	std::vector<float> captureBlock() override
	{
		std::vector<float> floatBuffer(m_config.periodSize);

		if (!isInitialized()) {
			DBG_ERROR("AlsaCapture not properly initialized");
			return floatBuffer;
		}

		int err = snd_pcm_readi(m_handle, m_buffer.data(), m_config.periodSize);
		if (err == -EPIPE) {
			DBG_WARN("Buffer overrun detected, recovering...");
			snd_pcm_prepare(m_handle);
			return floatBuffer;
		}
		else if (err < 0) {
			DBG_ERROR("Error reading from PCM device: " << std::string(snd_strerror(err)));
			return floatBuffer;
		}

		const uint8_t *src = m_buffer.data();
		for (size_t i = 0; i < m_config.periodSize; ++i) {
			float sample = 0.0f;

			if (m_config.processChannel < 0) {
				// Mix all channels
				for (unsigned int ch = 0; ch < m_config.channels; ++ch) {
					switch (m_config.format) {
					case SND_PCM_FORMAT_S16_LE: {
						const int16_t *samples = reinterpret_cast<const int16_t *>(src);
						sample += samples[i * m_config.channels + ch] / 32768.0f;
						break;
					}
					case SND_PCM_FORMAT_S32_LE: {
						const int32_t *samples = reinterpret_cast<const int32_t *>(src);
						sample += samples[i * m_config.channels + ch] / 2147483648.0f;
						break;
					}
					case SND_PCM_FORMAT_FLOAT_LE: {
						const float *samples = reinterpret_cast<const float *>(src);
						sample += samples[i * m_config.channels + ch];
						break;
					}
					default:
						DBG_ERROR("Unsupported format");
						return floatBuffer;
					}
				}
				// Normalize mixed channels
				sample /= m_config.channels;
				floatBuffer[i] = sample;
			} else {
				// Extract single channel
				switch (m_config.format) {
				case SND_PCM_FORMAT_S16_LE: {
					const int16_t *samples = reinterpret_cast<const int16_t *>(src);
					floatBuffer[i] = samples[i * m_config.channels + m_config.processChannel] / 32768.0f;
					break;
				}
				case SND_PCM_FORMAT_S32_LE: {
					const int32_t *samples = reinterpret_cast<const int32_t *>(src);
					floatBuffer[i] = samples[i * m_config.channels + m_config.processChannel] / 2147483648.0f;
					break;
				}
				case SND_PCM_FORMAT_FLOAT_LE: {
					const float *samples = reinterpret_cast<const float *>(src);
					floatBuffer[i] = samples[i * m_config.channels + m_config.processChannel];
					break;
				}
				default:
					DBG_ERROR("Unsupported format");
					return floatBuffer;
				}
			}
		}

		return floatBuffer;
	}

	unsigned int getSampleRate() const override
	{
		return m_config.sampleRate;
	}

	unsigned int getChannels() const override
	{
		return m_config.channels;
	}
};

int main(int argc, char *argv[])
{
	// Install stack trace handler
	nsdkStacktrace::install();

	// Set desired log level
	Logger::getInstance().setLogLevel(LOG_DEBUG);

	if (argc < 2) {
		printUsage(argv[0]);
		return 1;
	}

	auto [configValid, config] = parseCommandLine(argc, argv);
	if (!configValid) {
		printUsage(argv[0]);
		return 1;
	}

	// Set up signal handling
	signal(SIGINT, signalHandler);
	signal(SIGTERM, signalHandler);

	// Initialize FFTSyncCheck with custom parameters
	SyncCheckConfig syncConfig(config.blockSize, config.overlapPercent, config.sampleRate,
							 config.numBands, config.interpFactor);
	syncConfig.initBands(config.startFreq, config.freqRange, config.bandSep);

	// {
	// 	const float TEST_DURATION = 5.0f;  // Duration in seconds
	// 	const float BLOCK_DURATION = 0.1f; // Block duration in seconds

	// 	// Define test delays for each band in milliseconds
	// 	std::vector<float> test_delays = {
	// 		0.0f,   // Band 0 (reference)
	// 		2.0f,  // Band 1
	// 		1.0f,   // Band 2
	// 		1.0f,  // Band 3
	// 		1.0f,   // Band 4
	// 		1.0f,  // Band 5
	// 		1.0f,   // Band 6
	// 		1.0f   // Band 7
	// 	};

	// 	DBG_INFO("Generating test signal...");
	// 	TestSignalGenerator testSignalGenerator(syncConfig);
	// 	testSignalGenerator.enableWavDump(true, "test_signal");
	// 	std::vector<float> test_signal = testSignalGenerator.renderTestSignal(TEST_DURATION, test_delays);
	// 	DBG_INFO("Test signal generated, processing blocks...");

	// 	float elapsed_time = 0.0f;
	// 	FFTSyncCheck syncChecker(syncConfig);
	// 	for (size_t i = 0; i < test_signal.size(); i += static_cast<size_t>(BLOCK_DURATION * syncChecker.getSampleRate())) {
	// 		size_t block_size = std::min(static_cast<size_t>(BLOCK_DURATION * syncChecker.getSampleRate()), test_signal.size() - i);
	// 		std::vector<float> block(test_signal.begin() + i, test_signal.begin() + i + block_size);
	// 		syncChecker.processSampleBlock(block);

	// 		elapsed_time += BLOCK_DURATION;
	// 		if (elapsed_time >= 1.0f) {
	// 			syncChecker.printDelayStats(test_delays);
	// 			elapsed_time = 0.0f;
	// 		}
	// 	}
	// }

	// Initialize appropriate capture source
	std::unique_ptr<AudioSource> capture;
	if (config.captureDevice.substr(0, 7) == "file://") {
		capture = std::make_unique<FileCapture>(config);
	} else {
		capture = std::make_unique<AlsaCapture>(config);
	}

	if (!capture->isInitialized()) {
		DBG_ERROR("Failed to initialize audio capture");
		return 1;
	}

	// make sure we use the actual sampling rate from the capture device
	config.sampleRate = capture->getSampleRate();

	// Initialize visualizer
	DelayStatsVisualizer visualizer(config.outputFile, config.useColors);

	DBG_INFO("Starting capture from device: " << config.captureDevice);
	DBG_INFO("Sample rate: " << config.sampleRate);
	DBG_INFO("Channels: " << config.channels);
	DBG_INFO("Processing channel: " << config.processChannel);
	DBG_INFO("Block size: " << config.blockSize);
	DBG_INFO("Overlap: " << config.overlapPercent << "%");
	DBG_INFO("Number of bands: " << config.numBands);
	DBG_INFO("Interpolation factor: " << config.interpFactor);
	DBG_INFO("Start frequency: " << config.startFreq);
	DBG_INFO("Frequency range: " << config.freqRange);
	DBG_INFO("Band separation: " << config.bandSep);

	std::string bands;
	for (int band_idx : config.bandsOfInterest) {
		if (!bands.empty()) bands += ", ";
		bands += "Band " + std::to_string(band_idx);
	}
	DBG_INFO("Bands of interest: " << bands);

	unsigned int bandId = 1;
	for (const auto& band : syncConfig.getBands()) {
		DBG_INFO("Band " << bandId++ << ": " << band.getStartFreq() << " Hz - " << band.getEndFreq() << " Hz");
	}

	float statsUpdateTime = 0.0f;

	FFTSyncCheck syncChecker(syncConfig);

	// Processing buffer to hold block_size samples
	std::vector<float> processingBuffer(config.blockSize, 0.0f);
	size_t bufferFilled = 0;

	// Initialize WAV dump file if requested
	SNDFILE* dumpFile = nullptr;
	SF_INFO sfInfo = {};
	if (!config.dumpFile.empty()) {
		sfInfo.samplerate = config.sampleRate;
		sfInfo.channels = 1;
		sfInfo.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

		dumpFile = sf_open(config.dumpFile.c_str(), SFM_WRITE, &sfInfo);
		if (!dumpFile) {
			DBG_ERROR("Failed to open dump file: " << config.dumpFile << " (" << sf_strerror(nullptr) << ")");
			return 1;
		}
		DBG_INFO("Dumping audio to: " << config.dumpFile);
	}

	while (g_running) {
		// Get new samples
		std::vector<float> newSamples = capture->captureBlock();

		// Dump audio if enabled
		if (dumpFile) {
			sf_writef_float(dumpFile, newSamples.data(), newSamples.size());
		}

		// Copy new samples into processing buffer
		size_t samplesToProcess = std::min(newSamples.size(), config.blockSize - bufferFilled);
		std::copy(newSamples.begin(), newSamples.begin() + samplesToProcess, processingBuffer.begin() + bufferFilled);

		bufferFilled += samplesToProcess;

		// Process when we have a full block
		if (bufferFilled >= config.blockSize) {
			// Process the full block
			syncChecker.processSampleBlock(processingBuffer);

			// Calculate hop size based on overlap percentage
			size_t overlapSize = (config.blockSize * config.overlapPercent) / 100;
			size_t hopSize = config.blockSize - overlapSize;

			// Shift data by hop size
			if (overlapSize > 0) {
				std::copy(processingBuffer.begin() + hopSize, processingBuffer.begin() + config.blockSize, processingBuffer.begin());
				bufferFilled = overlapSize;
			} else {
				bufferFilled = 0;
			}

			statsUpdateTime += float(hopSize) / config.sampleRate;
			if (statsUpdateTime >= config.statsRefreshRate) {
				std::vector<std::pair<float, float>> estimates = syncChecker.getDelayStats();
				visualizer.visualizeDelays(estimates, config.bandsOfInterest);
				statsUpdateTime = 0.0f;
			}
		}
	}

	// Cleanup
	if (dumpFile) {
		sf_close(dumpFile);
	}

	DBG_INFO("Capture stopped.");
	return 0;
}