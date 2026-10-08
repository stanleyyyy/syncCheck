#include "NoiseSignalGenerator.h"
#include <algorithm>
#include <cmath>
#include <random>

std::vector<float> generateNoiseTestSignal(unsigned int channels, unsigned int sampleRate, float duration,
	const std::vector<float> &delaysMs, uint32_t seed)
{
	std::vector<long> delayFrames(channels, 0);
	for (unsigned int ch = 0; ch < channels && ch < delaysMs.size(); ch++) {
		delayFrames[ch] = std::lround(delaysMs[ch] * sampleRate / 1000.0f);
	}
	const long minDelay = *std::min_element(delayFrames.begin(), delayFrames.end());
	const long maxDelay = *std::max_element(delayFrames.begin(), delayFrames.end());

	// Noise at -16 dBFS RMS, long enough to cover all delays.
	const std::size_t frames = static_cast<std::size_t>(duration * sampleRate);
	std::vector<float> noise(frames + (maxDelay - minDelay));
	std::mt19937 rng(seed);
	std::normal_distribution<float> dist(0.0f, 0.16f);
	for (float &sample : noise) {
		sample = std::clamp(dist(rng), -1.0f, 1.0f);
	}

	std::vector<float> output(frames * channels);
	for (std::size_t i = 0; i < frames; i++) {
		for (unsigned int ch = 0; ch < channels; ch++) {
			output[i * channels + ch] = noise[i + (maxDelay - delayFrames[ch])];
		}
	}
	return output;
}
