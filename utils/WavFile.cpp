#include "WavFile.h"
#include "miniaudio.h"
#include <algorithm>
#include <cstdint>
#include <vector>

struct WavWriter::Impl
{
	ma_encoder encoder;
};

WavWriter::WavWriter() = default;

WavWriter::~WavWriter()
{
	close();
}

bool WavWriter::open(const std::string& path, unsigned int channels, unsigned int sampleRate)
{
	close();

	auto impl = std::make_unique<Impl>();
	ma_encoder_config config = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, channels, sampleRate);
	if (ma_encoder_init_file(path.c_str(), &config, &impl->encoder) != MA_SUCCESS) {
		return false;
	}

	m_impl = std::move(impl);
	return true;
}

std::size_t WavWriter::writeFrames(const float* data, std::size_t frames)
{
	if (!m_impl) {
		return 0;
	}

	ma_uint64 written = 0;
	ma_encoder_write_pcm_frames(&m_impl->encoder, data, frames, &written);
	return static_cast<std::size_t>(written);
}

bool WavWriter::isOpen() const
{
	return m_impl != nullptr;
}

void WavWriter::close()
{
	if (m_impl) {
		ma_encoder_uninit(&m_impl->encoder);
		m_impl.reset();
	}
}

struct WavReader::Impl
{
	ma_decoder decoder;
};

WavReader::WavReader() = default;

WavReader::~WavReader()
{
	close();
}

bool WavReader::open(const std::string& path)
{
	close();

	auto impl = std::make_unique<Impl>();
	ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
	if (ma_decoder_init_file(path.c_str(), &config, &impl->decoder) != MA_SUCCESS) {
		return false;
	}

	m_impl = std::move(impl);
	return true;
}

std::size_t WavReader::readFrames(float* data, std::size_t frames)
{
	if (!m_impl) {
		return 0;
	}

	ma_uint64 read = 0;
	ma_decoder_read_pcm_frames(&m_impl->decoder, data, frames, &read);
	return static_cast<std::size_t>(read);
}

bool WavReader::seek(std::size_t frame)
{
	return m_impl && ma_decoder_seek_to_pcm_frame(&m_impl->decoder, frame) == MA_SUCCESS;
}

bool WavReader::isOpen() const
{
	return m_impl != nullptr;
}

unsigned int WavReader::getChannels() const
{
	return m_impl ? m_impl->decoder.outputChannels : 0;
}

unsigned int WavReader::getSampleRate() const
{
	return m_impl ? m_impl->decoder.outputSampleRate : 0;
}

std::size_t WavReader::getLengthInFrames() const
{
	ma_uint64 length = 0;
	if (m_impl) {
		ma_decoder_get_length_in_pcm_frames(&m_impl->decoder, &length);
	}
	return static_cast<std::size_t>(length);
}

void WavReader::close()
{
	if (m_impl) {
		ma_decoder_uninit(&m_impl->decoder);
		m_impl.reset();
	}
}

namespace
{

void putLE(std::FILE* file, uint32_t value, int bytes)
{
	for (int i = 0; i < bytes; i++) {
		std::fputc(static_cast<int>((value >> (8 * i)) & 0xff), file);
	}
}

constexpr long WAV_HEADER_SIZE = 44;

} // namespace

WavRecorder::~WavRecorder()
{
	close();
}

bool WavRecorder::open(const std::string& path, unsigned int channels, unsigned int sampleRate)
{
	close();
	m_file = std::fopen(path.c_str(), "wb");
	if (!m_file) {
		return false;
	}
	m_channels = channels;
	m_sampleRate = sampleRate;
	m_framesWritten = 0;
	m_framesSinceUpdate = 0;
	updateHeader();
	return true;
}

void WavRecorder::writeFrames(const float* data, std::size_t frames)
{
	if (!m_file) {
		return;
	}

	std::vector<int16_t> pcm(frames * m_channels);
	for (std::size_t i = 0; i < pcm.size(); i++) {
		const float sample = std::clamp(data[i], -1.0f, 1.0f);
		pcm[i] = static_cast<int16_t>(sample * 32767.0f);
	}
	std::fwrite(pcm.data(), sizeof(int16_t), pcm.size(), m_file);
	m_framesWritten += frames;
	m_framesSinceUpdate += frames;

	// Keep the header current (about twice per second) so readers see the new data.
	if (m_framesSinceUpdate >= m_sampleRate / 2) {
		updateHeader();
		m_framesSinceUpdate = 0;
	}
}

void WavRecorder::updateHeader()
{
	const uint32_t dataSize = static_cast<uint32_t>(m_framesWritten * m_channels * sizeof(int16_t));
	const uint32_t blockAlign = m_channels * sizeof(int16_t);

	std::fseek(m_file, 0, SEEK_SET);
	std::fwrite("RIFF", 1, 4, m_file);
	putLE(m_file, 36 + dataSize, 4);
	std::fwrite("WAVEfmt ", 1, 8, m_file);
	putLE(m_file, 16, 4);			// fmt chunk size
	putLE(m_file, 1, 2);			// PCM
	putLE(m_file, m_channels, 2);
	putLE(m_file, m_sampleRate, 4);
	putLE(m_file, m_sampleRate * blockAlign, 4);
	putLE(m_file, blockAlign, 2);
	putLE(m_file, 16, 2);			// bits per sample
	std::fwrite("data", 1, 4, m_file);
	putLE(m_file, dataSize, 4);
	std::fseek(m_file, WAV_HEADER_SIZE + static_cast<long>(dataSize), SEEK_SET);
	std::fflush(m_file);
}

void WavRecorder::close()
{
	if (m_file) {
		updateHeader();
		std::fclose(m_file);
		m_file = nullptr;
	}
}
