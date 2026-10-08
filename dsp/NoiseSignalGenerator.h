#pragma once

#include <cstdint>
#include <vector>

/**
 * @brief Generate a multichannel test signal for channel mode measurements.
 *
 * Every channel carries the same white noise, optionally delayed per channel.
 *
 * @param channels Number of channels.
 * @param sampleRate Sample rate in Hz.
 * @param duration Duration in seconds.
 * @param delaysMs Delay of each channel in ms (missing entries are 0, positive = later).
 * @param seed Noise seed; equal seeds produce equal signals.
 * @return Interleaved samples.
 */
std::vector<float> generateNoiseTestSignal(unsigned int channels, unsigned int sampleRate, float duration,
	const std::vector<float> &delaysMs, uint32_t seed = 1);
