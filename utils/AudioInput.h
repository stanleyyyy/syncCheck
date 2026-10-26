#pragma once

#include "WavFile.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

class AudioSource
{
public:
	virtual ~AudioSource() = default;
	virtual bool isInitialized() const = 0;

	/**
	 * @brief Capture the next block of interleaved samples.
	 * @return Captured samples, or an empty buffer at end of stream or after stop().
	 */
	virtual std::vector<float> captureBlock() = 0;
	virtual unsigned int getSampleRate() const = 0;
	virtual unsigned int getChannels() const = 0;

	/**
	 * @brief Reason why the source failed to initialize, empty if unknown.
	 */
	virtual std::string getError() const { return std::string(); }

	/**
	 * @brief Make captureBlock() return an empty buffer. Safe to call from any thread.
	 */
	virtual void stop() { m_stopped = true; }

protected:
	std::atomic<bool> m_stopped { false };
};

class FileCapture : public AudioSource
{
private:
	std::string m_filePath;
	unsigned int m_periodSize;
	unsigned int m_channels;
	unsigned int m_sampleRate;
	WavReader m_file;
	bool m_initialized;
	bool m_realtime;
	std::size_t m_framesDelivered;
	std::chrono::steady_clock::time_point m_startTime;

public:
	/**
	 * @brief Constructor for FileCapture.
	 * @param captureDevice Path to audio file (may include a "file://" prefix).
	 * @param periodSize Number of frames per capture block.
	 * @param realtime Deliver blocks at the file's sample rate instead of as fast as possible.
	 */
	FileCapture(const std::string &captureDevice, unsigned int periodSize, bool realtime = false);

	bool isInitialized() const override { return m_initialized; }
	std::vector<float> captureBlock() override;
	unsigned int getSampleRate() const override { return m_sampleRate; }
	unsigned int getChannels() const override { return m_channels; }
};

/**
 * @brief Captures audio from a sound card through miniaudio.
 */
class DeviceCapture : public AudioSource
{
public:
	/**
	 * @brief Constructor for DeviceCapture.
	 * @param device Device specification, see AudioDevices.
	 * @param channels Number of channels to capture.
	 * @param sampleRate Desired sample rate.
	 * @param periodSize Period size in frames.
	 * @param periods Number of periods.
	 */
	DeviceCapture(const std::string &device, unsigned int channels, unsigned int sampleRate, unsigned int periodSize,
				  unsigned int periods);
	~DeviceCapture() override;

	bool isInitialized() const override;
	std::vector<float> captureBlock() override;
	unsigned int getSampleRate() const override { return m_sampleRate; }
	unsigned int getChannels() const override { return m_channels; }
	std::string getError() const override;

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
	unsigned int m_channels;
	unsigned int m_sampleRate;
	unsigned int m_periodSize;
};
