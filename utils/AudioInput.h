#pragma once

#include "SyncCheckConfig.h"
#include "Logger.h"
#include <sndfile.h>
#include <vector>
#include <cstring>
#include <string>

class AudioSource
{
public:
	virtual ~AudioSource() = default;
	virtual bool isInitialized() const = 0;
	virtual std::vector<float> captureBlock() = 0;
	virtual unsigned int getSampleRate() const = 0;
	virtual unsigned int getChannels() const = 0;
};

class FileCapture : public AudioSource
{
private:
	// Removed AppConfig m_config
	// Use individual members instead:
	std::string m_filePath;
	unsigned int m_periodSize;
	bool m_multichannel;
	unsigned int m_channels;
	unsigned int m_sampleRate;
	SNDFILE *m_file;
	SF_INFO m_sfInfo;
	std::vector<float> m_buffer;
	bool m_initialized;

public:
	/**
	 * @brief Constructor for FileCapture.
	 * @param captureDevice Path to audio file (may include a "file://" prefix).
	 * @param periodSize Number of frames per capture block.
	 * @param multichannel Flag indicating whether to capture multichannel data.
	 */
	FileCapture(const std::string &captureDevice, unsigned int periodSize, bool multichannel)
		: m_periodSize(periodSize)
		, m_multichannel(multichannel)
		, m_file(nullptr)
		, m_initialized(false)
	{
		// Remove "file://" prefix if present.
		m_filePath = captureDevice;
		const std::string prefix = "file://";
		if (m_filePath.find(prefix) == 0)
			m_filePath = m_filePath.substr(prefix.size());

		memset(&m_sfInfo, 0, sizeof(m_sfInfo));
		m_file = sf_open(m_filePath.c_str(), SFM_READ, &m_sfInfo);

		if (!m_file) {
			DBG_ERROR("Cannot open audio file: " << m_filePath << " (" << sf_strerror(nullptr) << ")");
			return;
		}

		// Override file properties.
		m_channels = m_sfInfo.channels;
		m_sampleRate = m_sfInfo.samplerate;

		m_buffer.resize(m_periodSize * m_channels);
		m_initialized = true;

		DBG_INFO("Opened audio file: " << m_filePath);
		DBG_INFO("Sample rate: " << m_sampleRate << " Hz");
		DBG_INFO("Channels: " << m_channels);
		DBG_INFO("Format: 0x" << std::hex << m_sfInfo.format << std::dec);
	}

	~FileCapture()
	{
		if (m_file) {
			sf_close(m_file);
		}
	}

	bool isInitialized() const override { return m_initialized; }

	std::vector<float> captureBlock() override
	{
		std::vector<float> output;
		if (!isInitialized()) {
			// Return silence.
			if (m_multichannel)
				output.resize(m_periodSize * m_channels, 0.0f);
			else
				output.resize(m_periodSize, 0.0f);
			return output;
		}

		sf_count_t framesRead = sf_readf_float(m_file, m_buffer.data(), m_periodSize);

		if (framesRead == 0) {
			// End of file. return empty buffer.
			return output;
		}

		if (m_multichannel) {
			std::vector<float> multiOutput(framesRead * m_channels, 0.0f);
			std::copy(m_buffer.begin(), m_buffer.begin() + framesRead * m_channels, multiOutput.begin());
			if (framesRead < static_cast<sf_count_t>(m_periodSize))
				std::fill(multiOutput.begin() + framesRead * m_channels, multiOutput.end(), 0.0f);
			return multiOutput;
		}
		else {
			std::vector<float> mixedOutput(framesRead, 0.0f);
			for (sf_count_t i = 0; i < framesRead; ++i) {
				float sum = 0.0f;
				for (unsigned int ch = 0; ch < m_channels; ++ch)
					sum += m_buffer[i * m_channels + ch];
				mixedOutput[i] = sum / m_channels;
			}
			if (framesRead < static_cast<sf_count_t>(m_periodSize))
				mixedOutput.resize(m_periodSize, 0.0f);
			return mixedOutput;
		}
	}

	unsigned int getSampleRate() const override { return m_sampleRate; }

	unsigned int getChannels() const override { return m_channels; }
};

class AlsaCapture : public AudioSource
{
private:
	std::string m_device;
	unsigned int m_channels;
	snd_pcm_format_t m_format;
	unsigned int m_sampleRate;
	snd_pcm_uframes_t m_periodSize;
	unsigned int m_periods;
	bool m_multichannel;
	snd_pcm_t *m_handle;
	std::vector<uint8_t> m_buffer;
	unsigned int m_frameSize;
	bool m_initialized;

public:
	/**
	 * @brief Constructor for AlsaCapture.
	 * @param device ALSA device name.
	 * @param channels Number of channels to capture.
	 * @param format Sample format.
	 * @param sampleRate Desired sample rate.
	 * @param periodSize Period size in frames.
	 * @param periods Number of periods.
	 * @param multichannel Flag indicating whether to capture multichannel data.
	 */
	AlsaCapture(const std::string &device, unsigned int channels, snd_pcm_format_t format, unsigned int sampleRate, unsigned int periodSize,
				unsigned int periods, bool multichannel)
		: m_device(device)
		, m_channels(channels)
		, m_format(format)
		, m_sampleRate(sampleRate)
		, m_periodSize(periodSize)
		, m_periods(periods)
		, m_multichannel(multichannel)
		, m_handle(nullptr)
		, m_frameSize(0)
		, m_initialized(false)
	{
		int err;
		if ((err = snd_pcm_open(&m_handle, m_device.c_str(), SND_PCM_STREAM_CAPTURE, 0)) < 0) {
			DBG_ERROR("Cannot open audio device: " << m_device << " (" << std::string(snd_strerror(err)) << ")");
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

		if ((err = snd_pcm_hw_params_set_format(m_handle, hw_params, m_format)) < 0) {
			DBG_ERROR("Cannot set sample format: " << std::string(snd_strerror(err)));
			return;
		}

		if ((err = snd_pcm_hw_params_set_channels(m_handle, hw_params, m_channels)) < 0) {
			DBG_ERROR("Cannot set channel count: " << std::string(snd_strerror(err)));
			return;
		}

		unsigned int rate = m_sampleRate;
		if ((err = snd_pcm_hw_params_set_rate_near(m_handle, hw_params, &rate, 0)) < 0) {
			DBG_ERROR("Cannot set sample rate: " << std::string(snd_strerror(err)));
			return;
		}
		m_sampleRate = rate;

		if ((err = snd_pcm_hw_params_set_period_size_near(m_handle, hw_params, &m_periodSize, 0)) < 0) {
			DBG_ERROR("Cannot set period size: " << std::string(snd_strerror(err)));
			return;
		}

		if ((err = snd_pcm_hw_params_set_periods_near(m_handle, hw_params, &m_periods, 0)) < 0) {
			DBG_ERROR("Cannot set periods: " << std::string(snd_strerror(err)));
			return;
		}

		if ((err = snd_pcm_hw_params(m_handle, hw_params)) < 0) {
			DBG_ERROR("Cannot set parameters: " << std::string(snd_strerror(err)));
			return;
		}

		m_frameSize = (snd_pcm_format_width(m_format) / 8) * m_channels;
		m_buffer.resize(m_periodSize * m_frameSize);
		m_initialized = true;
	}

	~AlsaCapture()
	{
		if (m_handle) {
			snd_pcm_close(m_handle);
		}
	}

	bool isInitialized() const override { return m_handle != nullptr && m_frameSize > 0 && m_initialized; }

	std::vector<float> captureBlock() override
	{
		std::vector<float> floatBuffer;
		if (!isInitialized()) {
			DBG_ERROR("AlsaCapture not properly initialized");
			if (m_multichannel)
				floatBuffer.resize(m_periodSize * m_channels, 0.0f);
			else
				floatBuffer.resize(m_periodSize, 0.0f);
			return floatBuffer;
		}

		int err = snd_pcm_readi(m_handle, m_buffer.data(), m_periodSize);
		if (err == -EPIPE) {
			DBG_WARN("Buffer overrun detected, recovering...");
			snd_pcm_prepare(m_handle);
			if (m_multichannel)
				floatBuffer.resize(m_periodSize * m_channels, 0.0f);
			else
				floatBuffer.resize(m_periodSize, 0.0f);
			return floatBuffer;
		}
		else if (err < 0) {
			DBG_ERROR("Error reading from PCM device: " << std::string(snd_strerror(err)));
			if (m_multichannel)
				floatBuffer.resize(m_periodSize * m_channels, 0.0f);
			else
				floatBuffer.resize(m_periodSize, 0.0f);
			return floatBuffer;
		}

		const uint8_t *src = m_buffer.data();

		if (m_multichannel) {
			std::vector<float> multiBuffer(err * m_channels, 0.0f);
			for (size_t i = 0; i < static_cast<size_t>(err); ++i) {
				for (unsigned int ch = 0; ch < m_channels; ++ch) {
					switch (m_format) {
					case SND_PCM_FORMAT_S16_LE: {
						const int16_t *samples = reinterpret_cast<const int16_t *>(src);
						multiBuffer[i * m_channels + ch] = samples[i * m_channels + ch] / 32768.0f;
						break;
					}
					case SND_PCM_FORMAT_S32_LE: {
						const int32_t *samples = reinterpret_cast<const int32_t *>(src);
						multiBuffer[i * m_channels + ch] = samples[i * m_channels + ch] / 2147483648.0f;
						break;
					}
					case SND_PCM_FORMAT_FLOAT_LE: {
						const float *samples = reinterpret_cast<const float *>(src);
						multiBuffer[i * m_channels + ch] = samples[i * m_channels + ch];
						break;
					}
					default:
						DBG_ERROR("Unsupported format");
						return multiBuffer;
					}
				}
			}
			return multiBuffer;
		}
		else {
			std::vector<float> mixedBuffer(err, 0.0f);
			for (size_t i = 0; i < static_cast<size_t>(err); ++i) {
				float sample = 0.0f;
				for (unsigned int ch = 0; ch < m_channels; ++ch) {
					switch (m_format) {
					case SND_PCM_FORMAT_S16_LE: {
						const int16_t *samples = reinterpret_cast<const int16_t *>(src);
						sample += samples[i * m_channels + ch] / 32768.0f;
						break;
					}
					case SND_PCM_FORMAT_S32_LE: {
						const int32_t *samples = reinterpret_cast<const int32_t *>(src);
						sample += samples[i * m_channels + ch] / 2147483648.0f;
						break;
					}
					case SND_PCM_FORMAT_FLOAT_LE: {
						const float *samples = reinterpret_cast<const float *>(src);
						sample += samples[i * m_channels + ch];
						break;
					}
					default:
						DBG_ERROR("Unsupported format");
						return mixedBuffer;
					}
				}
				mixedBuffer[i] = sample / m_channels;
			}
			return mixedBuffer;
		}
	}

	unsigned int getSampleRate() const override { return m_sampleRate; }

	unsigned int getChannels() const override { return m_channels; }
};
