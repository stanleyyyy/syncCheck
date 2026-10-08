#pragma once

#include <memory>
#include <string>
#include <vector>

/**
 * @brief Plays captured audio on a sound card through miniaudio (monitoring output).
 *
 * Blocks passed to playBlock() are queued and consumed by the device. If the queue
 * runs empty, silence is played; if it is full, new samples are dropped.
 */
class DevicePlayback
{
public:
	/**
	 * @brief Constructor for DevicePlayback.
	 * @param device Device specification, see AudioDevices.
	 * @param channels Number of interleaved channels passed to playBlock().
	 * @param sampleRate Sample rate.
	 * @param periodSize Period size in frames.
	 * @param periods Number of periods.
	 */
	DevicePlayback(const std::string &device, unsigned int channels, unsigned int sampleRate, unsigned int periodSize, unsigned int periods);
	~DevicePlayback();

	/**
	 * @brief Checks if the playback device was successfully initialized.
	 * @return True if initialized, false otherwise.
	 */
	bool isInitialized() const;

	/**
	 * @brief Queue a block of interleaved samples for playback.
	 * @param samples Vector containing audio samples in floating point.
	 * @return True if the whole block was queued.
	 */
	bool playBlock(const std::vector<float> &samples);

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
	unsigned int m_channels;
};
