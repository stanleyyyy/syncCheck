#include "AudioInput.h"
#include "AudioDeviceImpl.h"
#include "Logger.h"
#include <condition_variable>
#include <mutex>
#include <sstream>
#include <thread>

FileCapture::FileCapture(const std::string &captureDevice, unsigned int periodSize, bool realtime)
	: m_periodSize(periodSize)
	, m_channels(0)
	, m_sampleRate(0)
	, m_initialized(false)
	, m_realtime(realtime)
	, m_framesDelivered(0)
{
	// Remove "file://" prefix if present.
	m_filePath = captureDevice;
	const std::string prefix = "file://";
	if (m_filePath.find(prefix) == 0)
		m_filePath = m_filePath.substr(prefix.size());

	if (!m_file.open(m_filePath)) {
		DBG_ERROR("Cannot open audio file: " << m_filePath);
		return;
	}

	// Override file properties.
	m_channels = m_file.getChannels();
	m_sampleRate = m_file.getSampleRate();
	m_initialized = true;

	DBG_INFO("Opened audio file: " << m_filePath);
	DBG_INFO("Sample rate: " << m_sampleRate << " Hz");
	DBG_INFO("Channels: " << m_channels);
}

std::vector<float> FileCapture::captureBlock()
{
	if (!isInitialized() || m_stopped) {
		// Return empty buffer to signalize EOF
		return std::vector<float>();
	}

	std::vector<float> output(m_periodSize * m_channels, 0.0f);
	std::size_t framesRead = m_file.readFrames(output.data(), m_periodSize);
	if (framesRead == 0) {
		// End of file. return empty buffer.
		return std::vector<float>();
	}

	if (m_realtime) {
		if (m_framesDelivered == 0) {
			m_startTime = std::chrono::steady_clock::now();
		}
		std::this_thread::sleep_until(m_startTime + std::chrono::microseconds(m_framesDelivered * 1000000ull / m_sampleRate));
	}
	m_framesDelivered += framesRead;

	output.resize(framesRead * m_channels);
	return output;
}

struct DeviceCapture::Impl
{
	MiniaudioDevice m_device;
	ma_pcm_rb m_ringBuffer;
	bool m_ringBufferInitialized = false;
	std::mutex m_mutex;
	std::condition_variable m_dataAvailable;
	std::atomic<unsigned int> m_overruns { 0 };
	std::string m_error;

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
		(void)output;
		Impl *self = static_cast<Impl *>(device->pUserData);
		const unsigned int channels = device->capture.channels;
		const float *src = static_cast<const float *>(input);

		while (frameCount > 0) {
			ma_uint32 frames = frameCount;
			void *dst = nullptr;
			if (ma_pcm_rb_acquire_write(&self->m_ringBuffer, &frames, &dst) != MA_SUCCESS || frames == 0) {
				self->m_overruns++;
				break;
			}
			std::copy(src, src + frames * channels, static_cast<float *>(dst));
			ma_pcm_rb_commit_write(&self->m_ringBuffer, frames);
			src += frames * channels;
			frameCount -= frames;
		}

		self->m_dataAvailable.notify_one();
	}
};

DeviceCapture::DeviceCapture(const std::string &device, unsigned int channels, unsigned int sampleRate, unsigned int periodSize,
							 unsigned int periods)
	: m_impl(std::make_unique<Impl>())
	, m_channels(channels)
	, m_sampleRate(sampleRate)
	, m_periodSize(periodSize)
{
	ma_device_config config = ma_device_config_init(ma_device_type_capture);
	config.capture.format = ma_format_f32;
	config.capture.channels = channels;
	config.sampleRate = sampleRate;
	config.periodSizeInFrames = periodSize;
	config.periods = periods;
	config.dataCallback = Impl::dataCallback;
	config.pUserData = m_impl.get();
	// Never mix channels together: each delivered channel must be exactly one input of the device.
	// The default (rectangular) mode would e.g. fake channels 3 and 4 from a stereo stream.
	config.capture.channelMixMode = ma_channel_mix_mode_simple;

	// Plenty of headroom so that a slow consumer does not immediately overrun.
	if (ma_pcm_rb_init(ma_format_f32, channels, periodSize * periods * 8, nullptr, nullptr, &m_impl->m_ringBuffer) != MA_SUCCESS) {
		m_impl->m_error = "cannot allocate capture ring buffer";
		DBG_ERROR("Cannot allocate capture ring buffer");
		return;
	}
	m_impl->m_ringBufferInitialized = true;

	// Exclusive mode bypasses the Windows mixer, which may deliver fewer channels than the device has.
	std::string error;
	if (!m_impl->m_device.open(device, config, error, true)) {
		m_impl->m_error = error;
		DBG_ERROR("Cannot open audio device: " << device << " (" << error << ")");
		return;
	}

	ma_device &dev = m_impl->m_device.m_device;
	if (dev.capture.internalChannels < channels) {
		std::ostringstream msg;
		msg << "device " << dev.capture.name << " delivers only " << dev.capture.internalChannels << " of " << channels
			<< " requested channels";
		if (dev.pContext->backend == ma_backend_wasapi && !m_impl->m_device.m_exclusive) {
			msg << " in shared mode; close other applications using it so it can be opened exclusively";
		}
		m_impl->m_error = msg.str();
		DBG_ERROR("Cannot capture: " << m_impl->m_error);
		ma_device_uninit(&dev);
		m_impl->m_device.m_deviceInitialized = false;
		return;
	}
	if (dev.capture.internalChannels > channels) {
		DBG_WARN("Device " << dev.capture.name << " has " << dev.capture.internalChannels << " channels, "
			<< channels << " requested; the remaining channels are ignored");
	}
	if (dev.capture.internalSampleRate != sampleRate) {
		DBG_WARN("Device " << dev.capture.name << " runs at " << dev.capture.internalSampleRate << " Hz, "
			<< sampleRate << " Hz requested; audio is resampled");
	}

	if (ma_device_start(&dev) != MA_SUCCESS) {
		m_impl->m_error = "cannot start audio device";
		DBG_ERROR("Cannot start audio device: " << device);
		ma_device_uninit(&dev);
		m_impl->m_device.m_deviceInitialized = false;
		return;
	}

	DBG_INFO("Capturing from " << dev.capture.name << " (" << ma_get_backend_name(dev.pContext->backend)
		<< (m_impl->m_device.m_exclusive ? ", exclusive" : "") << ", " << dev.capture.internalChannels << " ch, "
		<< dev.capture.internalSampleRate << " Hz)");
}

DeviceCapture::~DeviceCapture() = default;

std::string DeviceCapture::getError() const
{
	return m_impl->m_error;
}

bool DeviceCapture::isInitialized() const
{
	return m_impl->m_device.m_deviceInitialized;
}

std::vector<float> DeviceCapture::captureBlock()
{
	if (!isInitialized()) {
		return std::vector<float>();
	}

	// Wait until a full period is available. The timeout keeps stop() responsive.
	std::unique_lock<std::mutex> lock(m_impl->m_mutex);
	while (ma_pcm_rb_available_read(&m_impl->m_ringBuffer) < m_periodSize) {
		if (m_stopped) {
			return std::vector<float>();
		}
		m_impl->m_dataAvailable.wait_for(lock, std::chrono::milliseconds(100));
	}
	lock.unlock();

	const unsigned int overruns = m_impl->m_overruns.exchange(0);
	if (overruns) {
		DBG_WARN("Buffer overrun detected, " << overruns << " capture callbacks dropped");
	}

	std::vector<float> output(m_periodSize * m_channels);
	float *dst = output.data();
	ma_uint32 remaining = m_periodSize;
	while (remaining > 0) {
		ma_uint32 frames = remaining;
		void *src = nullptr;
		ma_pcm_rb_acquire_read(&m_impl->m_ringBuffer, &frames, &src);
		std::copy(static_cast<float *>(src), static_cast<float *>(src) + frames * m_channels, dst);
		ma_pcm_rb_commit_read(&m_impl->m_ringBuffer, frames);
		dst += frames * m_channels;
		remaining -= frames;
	}
	return output;
}
