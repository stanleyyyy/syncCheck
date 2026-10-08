#pragma once

#include <cstddef>
#include <cstdio>
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

	/**
	 * @brief Move the read position to the given frame.
	 * @return True on success.
	 */
	bool seek(std::size_t frame);

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

/**
 * @brief Records interleaved samples to a 16-bit PCM WAV file.
 *
 * The header is refreshed regularly, so the file is a valid WAV file that can be read
 * (e.g. with WavReader) while the recording is still in progress.
 */
class WavRecorder
{
public:
	WavRecorder() = default;
	~WavRecorder();

	WavRecorder(const WavRecorder&) = delete;
	WavRecorder& operator=(const WavRecorder&) = delete;

	bool open(const std::string& path, unsigned int channels, unsigned int sampleRate);
	void writeFrames(const float* data, std::size_t frames);
	bool isOpen() const { return m_file != nullptr; }
	void close();

private:
	void updateHeader();

	std::FILE* m_file = nullptr;
	unsigned int m_channels = 0;
	unsigned int m_sampleRate = 0;
	std::size_t m_framesWritten = 0;
	std::size_t m_framesSinceUpdate = 0;
};
