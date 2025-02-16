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

/**
 * @brief Application configuration parameters.
 */
struct AppConfig
{
#ifdef BUILDTYPE_PC
	std::string		captureDevice = "hw:1"; // file://out.wav";
#else
	std::string		captureDevice = "default";
#endif
	unsigned int	sampleRate = 48000;
	snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;
	std::string		outputFile;
	bool			useColors = true;
	unsigned int	channels = 2;      // Physical channel count
	unsigned int	periodSize = 1024;
	unsigned int	periods = 2;
	unsigned int	blockSize = SyncCheckConfig::DEFAULT_BLOCK_SIZE;
	unsigned int	overlapPercent = SyncCheckConfig::DEFAULT_OVERLAP_PERCENT;
	unsigned int	numBands = SyncCheckConfig::DEFAULT_NUM_BANDS;
	unsigned int	interpFactor = SyncCheckConfig::DEFAULT_INTERP_FACTOR;
	unsigned int	startFreq = 18000;
	unsigned int	freqRange = 4000;
	unsigned int	bandSep = 100;
	bool			dumpEnabled = false;
	float			statsRefreshRate = 1.0f;  // Stats refresh rate in seconds
	bool			dumpCorrelation = false;

	/**
	 * @brief Unified source mapping.
	 *
	 * Provide "mix" to mix all physical channels or a CSV list of mappings.
	 * Each mapping is either a single number (physical channel, defaults to band 0)
	 * or in the form "physical:band" (e.g. "0:0,1:1" assigns physical channels 0 and 1
	 * to band 0 and band 1 respectively).
	 */
	std::string		sources;

	/**
	 * @brief Flag indicating whether to capture each physical channel individually.
	 * If false, capture code will mix all channels.
	 */
	bool			multichannel = false;
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

	/**
	 * @brief Visualize delay statistics for each channel assignment.
	 *
	 * Outputs channel assignment details along with delay, confidence, and PSLR.
	 *
	 * @param estimates Vector of tuples containing (ChannelAssignment, delay, confidence, PSLR) for each assignment.
	 */
	void visualizeDelays(const std::vector<std::tuple<FFTSyncCheck::ChannelAssignment, float, float, float>> &estimates)
	{
		std::stringstream ss;
		ss << std::fixed << std::setprecision(3);

		// Calculate center position
		int center = m_width / 2;
		ss << "Delay channel[band] -> channel[band] (ms), each division = " << (m_maxDelay / (center - 1)) << " ms\n";

		for (size_t i = 0; i < estimates.size(); ++i) {
			const FFTSyncCheck::ChannelAssignment &assignment = std::get<0>(estimates[i]);
			const double delay = std::get<1>(estimates[i]);
			const double confidence = std::get<2>(estimates[i]);
			const double pslr = std::get<3>(estimates[i]);

			// Skip if the assignment is to the same channel and band.
			if (assignment.refChannel == assignment.sigChannel && assignment.refBand == assignment.sigBand) {
				continue;
			}

			std::string color;
			if (m_useColors) {
				if (pslr > 5.0f)
					color = ANSI_COLOR_GREEN;
				else if (pslr > 2.0)
					color = ANSI_COLOR_YELLOW;
				else
					color = ANSI_COLOR_RED;
			}

			std::string assignStr = ""
								+ std::to_string(assignment.refChannel) + "["
								+ std::to_string(assignment.refBand) + "] -> "
								+ std::to_string(assignment.sigChannel) + "["
								+ std::to_string(assignment.sigBand) + "]";

			// Updated label: output "Assignment X" always.
			ss << (m_useColors ? color : "") << assignStr << ": "
			   << std::setw(8) << delay << " ms [PSLR: "
			   << std::fixed << std::setprecision(3) << std::setw(6) << pslr  // Fixed width of 6 for PSLR
			   << " dB, confidence: " << std::setw(6) << confidence << "]"
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

		// Output to console
		std::cout << ss.str() << std::endl << std::flush;

		// Output to file if opened
		if (m_outputFile.is_open()) {
			m_outputFile << std::setw(6) << std::setfill(' ') << m_dumpId++ << ", ";
			for (size_t i = 0; i < estimates.size(); ++i) {
				const FFTSyncCheck::ChannelAssignment &assignment = std::get<0>(estimates[i]);
				const double delay = std::get<1>(estimates[i]);
				const double confidence = std::get<2>(estimates[i]);
				const double pslr = std::get<3>(estimates[i]);

				// Skip if the assignment is to the same channel and band.
				if (assignment.refChannel == assignment.sigChannel && assignment.refBand == assignment.sigBand) {
					continue;
				}

				std::string assignStr = ""
								+ std::to_string(assignment.refChannel) + "["
								+ std::to_string(assignment.refBand) + "] -> "
								+ std::to_string(assignment.sigChannel) + "["
								+ std::to_string(assignment.sigBand) + "]";

				m_outputFile << "Assignment " << assignStr << ": "
				             << std::fixed << std::setprecision(3) << std::setw(8) << delay
				             << " ms, confidence: " << std::setw(5) << confidence << ", PSLR: "
				             << std::fixed << std::setprecision(3) << std::setw(5) << pslr;

				if (i < estimates.size() - 1)
					m_outputFile << ", ";
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
			  << "      --dump-correlation Enable correlation data dumping\n"
			  << "      --block-size=NUM   FFT block size (default: 8192)\n"
			  << "      --overlap=NUM      Overlap percent (50 or 75, default: 50)\n"
			  << "      --bands=NUM        Number of frequency bands (default: 8)\n"
			  << "      --interp=NUM       Interpolation factor (default: 1)\n"
			  << "      --start-freq=NUM   Start frequency in Hz (default: 18000)\n"
			  << "      --freq-range=NUM   Frequency range in Hz (default: 4000)\n"
			  << "      --band-sep=NUM     Band separation in Hz (default: 100)\n"
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
		// Depending on the mode we return either mixed mono samples or raw interleaved samples.
		// In mix mode: we mix all channels to a single stream.
		// In multichannel mode: we return all channels (raw interleaved).

		std::vector<float> output;
		if (!isInitialized()) {
			// Return silence: periodSize samples for mix mode, periodSize * channels for multichannel mode.
			if (m_config.multichannel)
				output.resize(m_config.periodSize * m_config.channels, 0.0f);
			else
				output.resize(m_config.periodSize, 0.0f);
			return output;
		}

		sf_count_t framesRead = sf_readf_float(m_file, m_buffer.data(), m_config.periodSize);

		if (framesRead == 0) {
			g_running = false; // End of file
			if (m_config.multichannel)
				output.resize(m_config.periodSize * m_config.channels, 0.0f);
			else
				output.resize(m_config.periodSize, 0.0f);
			return output;
		}

		// If in multichannel mode, return the raw interleaved data.
		if (m_config.multichannel) {
			std::vector<float> multiOutput(framesRead * m_config.channels, 0.0f);
			std::copy(m_buffer.begin(), m_buffer.begin() + framesRead * m_config.channels, multiOutput.begin());
			if (framesRead < static_cast<sf_count_t>(m_config.periodSize)) {
				std::fill(multiOutput.begin() + framesRead * m_config.channels, multiOutput.end(), 0.0f);
			}
			return multiOutput;
		} else {
			// Otherwise (mix mode), mix all channels to obtain one mono sample per frame.
			std::vector<float> mixedOutput(framesRead, 0.0f);
			for (sf_count_t i = 0; i < framesRead; ++i) {
				float sum = 0.0f;
				for (unsigned int ch = 0; ch < m_config.channels; ++ch) {
					sum += m_buffer[i * m_config.channels + ch];
				}
				mixedOutput[i] = sum / m_config.channels;
			}
			if (framesRead < static_cast<sf_count_t>(m_config.periodSize)) {
				mixedOutput.resize(m_config.periodSize, 0.0f);
			}
			return mixedOutput;
		}
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
		// In mix mode we will mix all physical channels and return a mono stream.
		// In multichannel mode we return raw interleaved data.
		std::vector<float> floatBuffer;
		if (!isInitialized()) {
			DBG_ERROR("AlsaCapture not properly initialized");
			if (m_config.multichannel)
				floatBuffer.resize(m_config.periodSize * m_config.channels, 0.0f);
			else
				floatBuffer.resize(m_config.periodSize, 0.0f);
			return floatBuffer;
		}

		int err = snd_pcm_readi(m_handle, m_buffer.data(), m_config.periodSize);
		if (err == -EPIPE) {
			DBG_WARN("Buffer overrun detected, recovering...");
			snd_pcm_prepare(m_handle);
			if (m_config.multichannel)
				floatBuffer.resize(m_config.periodSize * m_config.channels, 0.0f);
			else
				floatBuffer.resize(m_config.periodSize, 0.0f);
			return floatBuffer;
		} else if (err < 0) {
			DBG_ERROR("Error reading from PCM device: " << std::string(snd_strerror(err)));
			if (m_config.multichannel)
				floatBuffer.resize(m_config.periodSize * m_config.channels, 0.0f);
			else
				floatBuffer.resize(m_config.periodSize, 0.0f);
			return floatBuffer;
		}

		const uint8_t *src = m_buffer.data();

		if (m_config.multichannel) {
			// Process and return each channel individually.
			std::vector<float> multiBuffer(err * m_config.channels, 0.0f);
			for (size_t i = 0; i < static_cast<size_t>(err); ++i) {
				for (unsigned int ch = 0; ch < m_config.channels; ++ch) {
					switch (m_config.format) {
						case SND_PCM_FORMAT_S16_LE: {
							const int16_t *samples = reinterpret_cast<const int16_t *>(src);
							multiBuffer[i * m_config.channels + ch] = samples[i * m_config.channels + ch] / 32768.0f;
							break;
						}
						case SND_PCM_FORMAT_S32_LE: {
							const int32_t *samples = reinterpret_cast<const int32_t *>(src);
							multiBuffer[i * m_config.channels + ch] = samples[i * m_config.channels + ch] / 2147483648.0f;
							break;
						}
						case SND_PCM_FORMAT_FLOAT_LE: {
							const float *samples = reinterpret_cast<const float *>(src);
							multiBuffer[i * m_config.channels + ch] = samples[i * m_config.channels + ch];
							break;
						}
						default:
							DBG_ERROR("Unsupported format");
							return multiBuffer;
					}
				}
			}
			return multiBuffer;
		} else {
			// Mix mode: sum all channels per frame.
			std::vector<float> mixedBuffer(err, 0.0f);
			for (size_t i = 0; i < static_cast<size_t>(err); ++i) {
				float sample = 0.0f;
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
							return mixedBuffer;
					}
				}
				mixedBuffer[i] = sample / m_config.channels;
			}
			return mixedBuffer;
		}
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

	// After capture initialization, update sampleRate and channels accordingly.
	config.sampleRate = capture->getSampleRate();
	config.channels = capture->getChannels();

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

	// Initialize FFTSyncCheck.
	//
	// In mix mode we process a single mono channel.
	// Otherwise, we will build a custom channel assignment based on --sources.
	SyncCheckConfig syncConfig(config.blockSize, config.overlapPercent, config.sampleRate, config.numBands, config.interpFactor);
	syncConfig.initBands(config.startFreq, config.freqRange, config.bandSep);
	syncConfig.setDumpEnabled(config.dumpCorrelation);

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

	while (g_running) {
		std::vector<float> newSamples = capture->captureBlock();
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