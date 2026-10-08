#include "AudioOutput.h"
#include "AudioDeviceImpl.h"
#include "Logger.h"
#include <algorithm>

struct DevicePlayback::Impl
{
	MiniaudioDevice m_device;
	ma_pcm_rb m_ringBuffer;
	bool m_ringBufferInitialized = false;

	~Impl()
	{
		if (m_device.m_deviceInitialized) {
			ma_device_stop(&m_device.m_device);
		}
		if (m_ringBufferInitialized) {
			ma_pcm_rb_uninit(&m_ringBuffer);
		}
	}

	static void dataCallback(ma_device *device, void *output, const void *input, ma_uint32 frameCount)
	{
		(void)input;
		Impl *self = static_cast<Impl *>(device->pUserData);
		const unsigned int channels = device->playback.channels;
		float *dst = static_cast<float *>(output);

		while (frameCount > 0) {
			ma_uint32 frames = frameCount;
			void *src = nullptr;
			if (ma_pcm_rb_acquire_read(&self->m_ringBuffer, &frames, &src) != MA_SUCCESS || frames == 0) {
				break;
			}
			std::copy(static_cast<float *>(src), static_cast<float *>(src) + frames * channels, dst);
			ma_pcm_rb_commit_read(&self->m_ringBuffer, frames);
			dst += frames * channels;
			frameCount -= frames;
		}

		// Underrun: play silence.
		std::fill(dst, dst + frameCount * channels, 0.0f);
	}
};

DevicePlayback::DevicePlayback(const std::string &device, unsigned int channels, unsigned int sampleRate, unsigned int periodSize,
							   unsigned int periods)
	: m_impl(std::make_unique<Impl>())
	, m_channels(channels)
{
	ma_device_config config = ma_device_config_init(ma_device_type_playback);
	config.playback.format = ma_format_f32;
	config.playback.channels = channels;
	config.sampleRate = sampleRate;
	config.periodSizeInFrames = periodSize;
	config.periods = periods;
	config.dataCallback = Impl::dataCallback;
	config.pUserData = m_impl.get();

	if (ma_pcm_rb_init(ma_format_f32, channels, periodSize * periods * 8, nullptr, nullptr, &m_impl->m_ringBuffer) != MA_SUCCESS) {
		DBG_ERROR("Cannot allocate playback ring buffer");
		return;
	}
	m_impl->m_ringBufferInitialized = true;

	std::string error;
	if (!m_impl->m_device.open(device, config, error)) {
		DBG_ERROR("Cannot open audio playback device: " << device << " (" << error << ")");
		return;
	}

	if (ma_device_start(&m_impl->m_device.m_device) != MA_SUCCESS) {
		DBG_ERROR("Cannot start audio playback device: " << device);
		ma_device_uninit(&m_impl->m_device.m_device);
		m_impl->m_device.m_deviceInitialized = false;
		return;
	}
}

DevicePlayback::~DevicePlayback() = default;

bool DevicePlayback::isInitialized() const
{
	return m_impl->m_device.m_deviceInitialized;
}

bool DevicePlayback::playBlock(const std::vector<float> &samples)
{
	if (!isInitialized()) {
		return false;
	}

	const float *src = samples.data();
	ma_uint32 remaining = static_cast<ma_uint32>(samples.size() / m_channels);
	while (remaining > 0) {
		ma_uint32 frames = remaining;
		void *dst = nullptr;
		if (ma_pcm_rb_acquire_write(&m_impl->m_ringBuffer, &frames, &dst) != MA_SUCCESS || frames == 0) {
			DBG_WARN("Playback queue full, dropping " << remaining << " frames");
			return false;
		}
		std::copy(src, src + frames * m_channels, static_cast<float *>(dst));
		ma_pcm_rb_commit_write(&m_impl->m_ringBuffer, frames);
		src += frames * m_channels;
		remaining -= frames;
	}
	return true;
}
