#pragma once
#include "Logger.h"
#include "SyncCheckConfig.h"
#include <alsa/asoundlib.h>
#include <string>
#include <vector>

/**
 * @brief ALSA playback class to output captured audio.
 *
 * This class opens an ALSA playback device using the details specified
 * in the AppConfig (via outputDevice) and writes blocks of audio samples.
 * It uses the same audio format as configured for ALSA capture and
 * takes into account the multichannel flag. In mix mode (multichannel == false)
 * the output is mono.
 */
class AlsaPlayback
{
private:
	std::string m_device;
	unsigned int m_channels;
	snd_pcm_format_t m_format;
	unsigned int m_sampleRate;
	snd_pcm_uframes_t m_periodSize;
	unsigned int m_periods;
	snd_pcm_t *m_handle;
	bool m_initialized;

public:
	/**
	 * @brief Constructor for AlsaPlayback.
	 * @param device ALSA output device name.
	 * @param channels Number of output channels.
	 * @param format Sample format.
	 * @param sampleRate Desired sample rate.
	 * @param periodSize Period size in frames.
	 * @param periods Number of periods.
	 */
	AlsaPlayback(const std::string &device, unsigned int channels, snd_pcm_format_t format, unsigned int sampleRate, unsigned int periodSize, unsigned int periods)
		: m_device(device)
		, m_channels(channels)
		, m_format(format)
		, m_sampleRate(sampleRate)
		, m_periodSize(periodSize)
		, m_periods(periods)
		, m_handle(nullptr)
		, m_initialized(false)
	{
		int err;
		if ((err = snd_pcm_open(&m_handle, m_device.c_str(), SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
			DBG_ERROR("Cannot open audio playback device: " << m_device << " (" << std::string(snd_strerror(err)) << ")");
			return;
		}

		snd_pcm_hw_params_t *hw_params;
		snd_pcm_hw_params_alloca(&hw_params);

		err = snd_pcm_hw_params_any(m_handle, hw_params);
		if (err < 0) {
			DBG_ERROR("Cannot initialize hardware parameter structure for output device: " << m_device << " (" << snd_strerror(err) << ")");
			return;
		}

		err = snd_pcm_hw_params_set_access(m_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
		if (err < 0) {
			DBG_ERROR("Cannot set access type for output device: " << m_device << " (" << snd_strerror(err) << ")");
			return;
		}

		// Use the same audio format as in capture.
		err = snd_pcm_hw_params_set_format(m_handle, hw_params, m_format);
		if (err < 0) {
			DBG_ERROR("Cannot set sample format for output device: " << m_device << " (" << snd_strerror(err) << ")");
			return;
		}

		// Set playback channels following the multichannel flag:
		// if multichannel is false (mix mode) then use 1 (mono) otherwise use config.channels.
		unsigned int playbackChannels = m_channels;
		err = snd_pcm_hw_params_set_channels(m_handle, hw_params, playbackChannels);
		if (err < 0) {
			DBG_ERROR("Cannot set channel count for output device: " << m_device << " (" << snd_strerror(err) << ")");
			return;
		}

		unsigned int rate = m_sampleRate;
		err = snd_pcm_hw_params_set_rate_near(m_handle, hw_params, &rate, 0);
		if (err < 0) {
			DBG_ERROR("Cannot set sample rate for output device: " << m_device << " (" << snd_strerror(err) << ")");
			return;
		}
		m_sampleRate = rate;

		snd_pcm_uframes_t bufferSize = m_periodSize * m_periods;
		err = snd_pcm_hw_params_set_buffer_size_near(m_handle, hw_params, &bufferSize);
		if (err < 0) {
			DBG_ERROR("Cannot set buffer size for output device: " << m_device << " (" << snd_strerror(err) << ")");
			return;
		}

		err = snd_pcm_hw_params_set_period_size_near(m_handle, hw_params, &m_periodSize, 0);
		if (err < 0) {
			DBG_ERROR("Cannot set period size for output device: " << m_device << " (" << snd_strerror(err) << ")");
			return;
		}

		err = snd_pcm_hw_params(m_handle, hw_params);
		if (err < 0) {
			DBG_ERROR("Cannot set hardware parameters for output device: " << m_device << " (" << snd_strerror(err) << ")");
			return;
		}

		snd_pcm_sw_params_t *swParams;
		snd_pcm_sw_params_alloca(&swParams);

		if ((err = snd_pcm_sw_params_current(m_handle, swParams)) < 0) {
			DBG_ERROR("Unable to determine current swparams for playback: " << snd_strerror(err));
			return;
		}

		if ((err = snd_pcm_sw_params_get_start_threshold(swParams, &bufferSize)) < 0) {
			DBG_ERROR("Unable to get start threshold mode for playback: " << snd_strerror(err));
			return;
		}

		if ((err = snd_pcm_sw_params(m_handle, swParams)) < 0) {
			DBG_ERROR("Unable to set sw params for playback: " << snd_strerror(err));
			return;
		}

		m_initialized = true;
		DBG_INFO("Audio output device initialized: " << m_device);
	}

	/**
	 * @brief Destructor that closes the ALSA playback device.
	 */
	~AlsaPlayback()
	{
		if (m_handle) {
			snd_pcm_close(m_handle);
		}
	}

	/**
	 * @brief Checks if the playback device was successfully initialized.
	 * @return True if initialized, false otherwise.
	 */
	bool isInitialized() const { return m_initialized; }

	/**
	 * @brief Plays a block of audio samples.
	 *
	 * Converts captured floating point samples into the configured
	 * audio format before writing to the output device, taking into account
	 * the multichannel flag. In mix mode, each frame consists of one sample.
	 *
	 * @param samples Vector containing audio samples in floating point.
	 * @return True if playback was successful, false otherwise.
	 */
	bool playBlock(const std::vector<float> &samples)
	{
		// Determine actual playback channel count.
		unsigned int playbackChannels = m_channels;
		// In mix mode, samples are mono (1 channel), otherwise they are interleaved.
		size_t frames = samples.size() / playbackChannels;
		int err = 0;
		switch (m_format) {
		case SND_PCM_FORMAT_FLOAT_LE: {
			err = snd_pcm_writei(m_handle, samples.data(), frames);
			break;
		}
		case SND_PCM_FORMAT_S16_LE: {
			std::vector<int16_t> buf(frames * playbackChannels);
			for (size_t i = 0; i < frames; i++) {
				for (unsigned int ch = 0; ch < playbackChannels; ch++) {
					// For multichannel mode, use interleaved samples;
					// in mix mode, use a single sample per frame.
					float s = m_channels > 1 ? samples[i * m_channels + ch] : samples[i];
					if (s > 1.0f)
						s = 1.0f;
					else if (s < -1.0f)
						s = -1.0f;
					buf[i * playbackChannels + ch] = static_cast<int16_t>(s * 32767);
				}
			}
			err = snd_pcm_writei(m_handle, buf.data(), frames);
			break;
		}
		case SND_PCM_FORMAT_S32_LE: {
			std::vector<int32_t> buf(frames * playbackChannels);
			for (size_t i = 0; i < frames; i++) {
				for (unsigned int ch = 0; ch < playbackChannels; ch++) {
					float s = m_channels > 1 ? samples[i * m_channels + ch] : samples[i];
					if (s > 1.0f)
						s = 1.0f;
					else if (s < -1.0f)
						s = -1.0f;
					buf[i * playbackChannels + ch] = static_cast<int32_t>(s * 2147483647);
				}
			}
			err = snd_pcm_writei(m_handle, buf.data(), frames);
			break;
		}
		default: {
			DBG_ERROR("Unsupported playback format");
			return false;
		}
		}

		if (err == -EPIPE) {
			DBG_WARN("Underrun occurred on output device, recovering...");

			err = snd_pcm_recover(m_handle, err, 0);
			if (err < 0) {
				DBG_ERROR("Alsa write fatally failed: " << snd_strerror(err));
				snd_pcm_close(m_handle);
				return false;
			}
			return false;
		}
		else if (err < 0) {
			DBG_ERROR("Error writing to output device: " << snd_strerror(err));
			return false;
		}

		return true;
	}
};
