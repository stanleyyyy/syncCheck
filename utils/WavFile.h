#pragma once

#include <cstddef>
#include <memory>
#include <string>

/**
 * @brief Writes interleaved 32-bit float samples to a WAV file.
 */
class WavWriter
{
public:
	WavWriter();
	~WavWriter();

	WavWriter(const WavWriter&) = delete;
	WavWriter& operator=(const WavWriter&) = delete;

	/**
	 * @brief Create (or truncate) a WAV file.
	 * @param path Output file path.
	 * @param channels Number of interleaved channels.
	 * @param sampleRate Sample rate in Hz.
	 * @return True on success.
	 */
	bool open(const std::string& path, unsigned int channels, unsigned int sampleRate);

	/**
	 * @brief Write interleaved frames.
	 * @return Number of frames written.
	 */
	std::size_t writeFrames(const float* data, std::size_t frames);

	bool isOpen() const;
	void close();

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};

/**
 * @brief Reads an audio file (WAV, FLAC or MP3) as interleaved 32-bit float samples.
 */
class WavReader
{
public:
	WavReader();
	~WavReader();

	WavReader(const WavReader&) = delete;
	WavReader& operator=(const WavReader&) = delete;

	/**
	 * @brief Open an audio file using its native channel count and sample rate.
	 * @return True on success.
	 */
	bool open(const std::string& path);

	/**
	 * @brief Read interleaved frames.
	 * @return Number of frames read, 0 at end of file.
	 */
	std::size_t readFrames(float* data, std::size_t frames);

	bool isOpen() const;
	unsigned int getChannels() const;
	unsigned int getSampleRate() const;

	/**
	 * @brief Total length of the file in frames, 0 if unknown.
	 */
	std::size_t getLengthInFrames() const;

	void close();

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};
