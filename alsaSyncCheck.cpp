#include <alsa/asoundlib.h>
#include <getopt.h>
#include <signal.h>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <sndfile.h>

#include "nsdkStacktrace.h"
#include "SyncCheck.h"
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
	std::string alsaDevice = "default";
	unsigned int sampleRate = 48000;
	snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;
	std::string outputFile;
	bool useColors = true;
	unsigned int channels = 1;
	unsigned int periodSize = 1024;
	unsigned int periods = 4;
	unsigned int processChannel = 0;  // Which channel to process
	unsigned int blockSize = SyncCheckConfig::DEFAULT_BLOCK_SIZE;
	unsigned int overlapPercent = SyncCheckConfig::DEFAULT_OVERLAP_PERCENT;
	unsigned int numBands = SyncCheckConfig::DEFAULT_NUM_BANDS;
	unsigned int interpFactor = SyncCheckConfig::DEFAULT_INTERP_FACTOR;
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
	std::chrono::steady_clock::time_point m_startTime;

public:
	DelayStatsVisualizer(const std::string &outputFile, bool useColors)
		: m_useColors(useColors)
		, m_startTime(std::chrono::steady_clock::now())
	{
		if (!outputFile.empty()) {
			m_outputFile.open(outputFile);
		}
	}

	void visualizeDelays(const std::vector<float> &delays)
	{
		auto now = std::chrono::steady_clock::now();
		float elapsed = std::chrono::duration<float>(now - m_startTime).count();

		std::stringstream ss;
		ss << std::fixed << std::setprecision(3);
		ss << "[" << elapsed << "s] Delays (ms):\n";

		for (size_t i = 0; i < delays.size(); ++i) {
			std::string color;
			if (m_useColors) {
				if (std::abs(delays[i]) < 0.1f)
					color = ANSI_COLOR_GREEN;
				else if (std::abs(delays[i]) < 0.5f)
					color = ANSI_COLOR_YELLOW;
				else
					color = ANSI_COLOR_RED;
			}

			ss << (m_useColors ? color : "") << "Band " << i << ": " << std::setw(8) << delays[i] << "ms" << (m_useColors ? ANSI_COLOR_RESET : "") << "\n";
		}
		ss << "\n";

		// Output to console
		std::cout << ss.str() << std::flush;

		// Output to file if opened
		if (m_outputFile.is_open()) {
			m_outputFile << elapsed << ",";
			for (size_t i = 0; i < delays.size(); ++i) {
				m_outputFile << delays[i];
				if (i < delays.size() - 1)
					m_outputFile << ",";
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
			  << "  -p, --period-size=NUM  Period size (default: 1024)\n"
			  << "      --process-ch=NUM   Channel to process (0-based, default: 0)\n"
			  << "      --block-size=NUM   FFT block size (default: 8192)\n"
			  << "      --overlap=NUM      Overlap percent (50 or 75, default: 50)\n"
			  << "      --bands=NUM        Number of frequency bands (default: 8)\n"
			  << "      --interp=NUM       Interpolation factor (default: 1)\n"
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
		{ "period-size", required_argument, 0, 'p' },
		{ "process-ch", required_argument, 0, 1 },
		{ "block-size", required_argument, 0, 2 },
		{ "overlap", required_argument, 0, 3 },
		{ "bands", required_argument, 0, 4 },
		{ "interp", required_argument, 0, 5 },
		{ "help", no_argument, 0, 'h' },
		{ 0, 0, 0, 0 }
	};

	int opt;
	while ((opt = getopt_long(argc, argv, "d:r:f:o:nc:p:h", long_options, nullptr)) != -1) {
		switch (opt) {
		case 'd':
			config.alsaDevice = optarg;
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
		case 1: // process-ch
			config.processChannel = std::stoi(optarg);
			break;
		case 2: // block-size
			config.blockSize = std::stoi(optarg);
			break;
		case 3: // overlap
			config.overlapPercent = std::stoi(optarg);
			if (config.overlapPercent != 50 && config.overlapPercent != 75) {
				DBG_ERROR("Invalid overlap percent. Must be 50 or 75");
				return { false, config };
			}
			break;
		case 4: // bands
			config.numBands = std::stoi(optarg);
			break;
		case 5: // interp
			config.interpFactor = std::stoi(optarg);
			break;
		case 'h':
			return { false, config };
		default:
			return { false, config };
		}
	}

	// Validate process channel
	if (config.processChannel >= config.channels) {
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
		std::string filepath = config.alsaDevice.substr(7); // Remove "file://" prefix

		memset(&m_sfInfo, 0, sizeof(m_sfInfo));
		m_file = sf_open(filepath.c_str(), SFM_READ, &m_sfInfo);

		if (!m_file) {
			DBG_ERROR("Cannot open audio file: " << filepath << " (" << sf_strerror(nullptr) << ")");
			return;
		}

		// Override config parameters with file properties
		m_config.channels = m_sfInfo.channels;
		m_config.sampleRate = m_sfInfo.samplerate;

		if (m_config.processChannel >= m_config.channels) {
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

		// Extract the requested channel
		for (sf_count_t i = 0; i < framesRead; ++i) {
			output[i] = m_buffer[i * m_config.channels + m_config.processChannel];
		}

		// Zero-pad if we read less than requested
		if (framesRead < m_config.periodSize) {
			std::fill(output.begin() + framesRead, output.end(), 0.0f);
		}

		return output;
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
		if ((err = snd_pcm_open(&m_handle, config.alsaDevice.c_str(), SND_PCM_STREAM_CAPTURE, 0)) < 0) {
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

		m_frameSize = snd_pcm_format_width(config.format) / 8 * config.channels;
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

		// Convert to float based on format, extracting only the specified channel
		const uint8_t *src = m_buffer.data();
		for (size_t i = 0; i < m_config.periodSize; ++i) {
			switch (m_config.format) {
			case SND_PCM_FORMAT_S16_LE: {
				const int16_t *samples = reinterpret_cast<const int16_t *>(src);
				floatBuffer[i] = samples[i * m_config.channels + m_config.processChannel] / 32768.0f;
				src += 2 * m_config.channels;
				break;
			}
			case SND_PCM_FORMAT_S32_LE: {
				const int32_t *samples = reinterpret_cast<const int32_t *>(src);
				floatBuffer[i] = samples[i * m_config.channels + m_config.processChannel] / 2147483648.0f;
				src += 4 * m_config.channels;
				break;
			}
			case SND_PCM_FORMAT_FLOAT_LE: {
				const float *samples = reinterpret_cast<const float *>(src);
				floatBuffer[i] = samples[i * m_config.channels + m_config.processChannel];
				src += 4 * m_config.channels;
				break;
			}
			default:
				DBG_ERROR("Unsupported format");
				return floatBuffer;
			}
		}

		return floatBuffer;
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

	FFTSyncCheck syncChecker(syncConfig);

	// Initialize appropriate capture source
	std::unique_ptr<AudioSource> capture;
	if (config.alsaDevice.substr(0, 7) == "file://") {
		capture = std::make_unique<FileCapture>(config);
	} else {
		capture = std::make_unique<AlsaCapture>(config);
	}

	if (!capture->isInitialized()) {
		DBG_ERROR("Failed to initialize audio capture");
		return 1;
	}

	// Initialize visualizer
	DelayStatsVisualizer visualizer(config.outputFile, config.useColors);

	DBG_INFO("Starting capture from device: " << config.alsaDevice);
	DBG_INFO("Sample rate: " << config.sampleRate);
	DBG_INFO("Channels: " << config.channels);
	DBG_INFO("Processing channel: " << config.processChannel);
	DBG_INFO("Block size: " << config.blockSize);
	DBG_INFO("Overlap: " << config.overlapPercent << "%");
	DBG_INFO("Number of bands: " << config.numBands);
	DBG_INFO("Interpolation factor: " << config.interpFactor);

	float statsUpdateTime = 0.0f;
	const float STATS_UPDATE_INTERVAL = 1.0f; // Update stats every second

	while (g_running) {
		std::vector<float> block = capture->captureBlock();
		syncChecker.processSampleBlock(block);

		statsUpdateTime += float(config.periodSize) / config.sampleRate;
		if (statsUpdateTime >= STATS_UPDATE_INTERVAL) {
			std::vector<float> delays = syncChecker.getDelayEstimates();
			visualizer.visualizeDelays(delays);
			statsUpdateTime = 0.0f;
		}
	}

	DBG_INFO("Capture stopped.");
	return 0;
}