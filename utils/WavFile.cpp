#include "WavFile.h"
#include "miniaudio.h"

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
