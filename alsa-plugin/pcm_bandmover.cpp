#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>
#include "BandProcessor.h"
#include "SyncCheckConfig.h"

#include <alsa/pcm_plugin.h>

// C++ code needs to be wrapped in extern "C" for ALSA
extern "C" {

typedef struct snd_pcm_bandmover {
	snd_pcm_extplug_t ext;
	SyncCheckConfig* config;
	BandProcessor* processor;  // Single processor instance
	unsigned int target_band;
	std::vector<float> processing_buffer;  // Buffer for block processing
	std::vector<float> output_buffer;      // Buffer to store processed output samples
	size_t filled_samples;                 // Number of samples currently in processing buffer
	size_t available_output_samples;       // Number of processed samples ready for output
	bool buffers_initialized;              // Flag to track buffer initialization
} snd_pcm_bandmover_t;

static inline void* bandmover_ext_to_priv(snd_pcm_extplug_t *ext) {
	return (void*)((char*)ext - offsetof(snd_pcm_bandmover_t, ext));
}

static snd_pcm_sframes_t bandmover_transfer(snd_pcm_extplug_t *ext,
	const snd_pcm_channel_area_t *dst_areas,
	snd_pcm_uframes_t dst_offset,
	const snd_pcm_channel_area_t *src_areas,
	snd_pcm_uframes_t src_offset,
	snd_pcm_uframes_t size)
{
	snd_pcm_bandmover_t *bandmover = (snd_pcm_bandmover_t*)bandmover_ext_to_priv(ext);

	// Get number of channels
	unsigned int channels = ext->channels;

	// Calculate pointers and steps for channel 0 (which we'll process)
	float *src = (float*)snd_pcm_channel_area_addr(&src_areas[0], src_offset);
	int src_step = snd_pcm_channel_area_step(&src_areas[0]) / sizeof(float);

	size_t block_size = bandmover->config->getBlockSize();
	size_t hop_size = bandmover->config->getHopSize();

	// Initialize buffers if needed
	if (!bandmover->buffers_initialized) {
		bandmover->processing_buffer.resize(block_size, 0.0f);
		bandmover->output_buffer.resize(block_size * 4, 0.0f);  // Make output buffer larger to store multiple processed blocks
		bandmover->filled_samples = 0;
		// Pre-fill output buffer with silence to avoid discontinuity
		bandmover->available_output_samples = block_size;  // Start with one block of silence
		bandmover->buffers_initialized = true;
	}

	// Copy input samples to processing buffer
	size_t input_pos = 0;
	while (input_pos < size) {
		// Calculate how many samples we can copy to processing buffer
		size_t space_in_buffer = block_size - bandmover->filled_samples;
		size_t samples_to_copy = std::min(size - input_pos, space_in_buffer);

		// Copy samples to processing buffer
		for (size_t i = 0; i < samples_to_copy; i++) {
			bandmover->processing_buffer[bandmover->filled_samples + i] = src[(input_pos + i) * src_step];
		}
		bandmover->filled_samples += samples_to_copy;
		input_pos += samples_to_copy;

		// Process only if we have a full block
		if (bandmover->filled_samples >= block_size) {
			if (bandmover->processor) {
				std::vector<float> block_output(block_size);
				// Low/high frequencies covering all the bands
				const SyncCheckConfig::Band clearBand = {
					bandmover->config->getBands()[0].getStartFreq(),
					bandmover->config->getBands()[bandmover->config->getNumBands() - 1].getEndFreq()
				};
				bandmover->processor->processBlockWithShift(bandmover->processing_buffer, bandmover->processing_buffer, block_output, clearBand);

				// Add processed samples (hop_size) to output buffer
				if (bandmover->available_output_samples + hop_size <= bandmover->output_buffer.size()) {
					std::copy(block_output.begin(),
							block_output.begin() + hop_size,
							bandmover->output_buffer.begin() + bandmover->available_output_samples);
					bandmover->available_output_samples += hop_size;
				}
			}

			// Shift data in processing buffer by hop_size
			std::copy(bandmover->processing_buffer.begin() + hop_size,
					bandmover->processing_buffer.begin() + bandmover->filled_samples,
					bandmover->processing_buffer.begin());
			bandmover->filled_samples -= hop_size;
		}
	}

	// Copy from output buffer to all destination channels
	size_t samples_to_output = std::min(size, bandmover->available_output_samples);
	size_t silence_samples = size - samples_to_output;  // Calculate how many silence samples we need

	// First output silence for any missing samples
	if (silence_samples > 0) {
		for (unsigned int ch = 0; ch < channels; ch++) {
			float *dst = (float*)snd_pcm_channel_area_addr(&dst_areas[ch], dst_offset);
			int dst_step = snd_pcm_channel_area_step(&dst_areas[ch]) / sizeof(float);

			for (size_t i = 0; i < silence_samples; i++) {
				dst[i * dst_step] = 0.0f;
			}
		}
	}

	// Then append any available processed samples
	if (samples_to_output > 0) {
		for (unsigned int ch = 0; ch < channels; ch++) {
			float *dst = (float*)snd_pcm_channel_area_addr(&dst_areas[ch], dst_offset);
			int dst_step = snd_pcm_channel_area_step(&dst_areas[ch]) / sizeof(float);

			// Copy with stride, starting after the silence samples
			for (size_t i = 0; i < samples_to_output; i++) {
				dst[(silence_samples + i) * dst_step] = bandmover->output_buffer[i];
			}
		}

		// Shift remaining samples in output buffer
		if (bandmover->available_output_samples > samples_to_output) {
			std::copy(bandmover->output_buffer.begin() + samples_to_output,
					bandmover->output_buffer.begin() + bandmover->available_output_samples,
					bandmover->output_buffer.begin());
		}
		bandmover->available_output_samples -= samples_to_output;
	}

	return size;  // Always return the full size as we've filled it with either silence or data
}

static int bandmover_init(snd_pcm_extplug_t *ext)
{
	snd_pcm_bandmover_t *bandmover = (snd_pcm_bandmover_t*)bandmover_ext_to_priv(ext);

	// Initialize state
	bandmover->filled_samples = 0;
	bandmover->available_output_samples = 0;
	bandmover->buffers_initialized = false;
	return 0;
}

static int bandmover_close(snd_pcm_extplug_t *ext)
{
	snd_pcm_bandmover_t *bandmover = (snd_pcm_bandmover_t*)bandmover_ext_to_priv(ext);

	// Cleanup
	delete bandmover->processor;
	delete bandmover->config;

	return 0;
}

static void bandmover_dump(snd_pcm_extplug_t *ext, snd_output_t *out)
{
	snd_pcm_bandmover_t *bandmover = (snd_pcm_bandmover_t*)bandmover_ext_to_priv(ext);

	if (bandmover->ext.name)
		snd_output_printf(out, "%s\n", bandmover->ext.name);
	else
		snd_output_printf(out, "Band Mover Plugin\n");

	snd_output_printf(out, "Its setup is:\n");
	snd_output_printf(out, "  channels     : %d\n", bandmover->ext.channels);
	snd_output_printf(out, "  target band  : %d\n", bandmover->target_band);
	snd_output_printf(out, "  block size   : %lu\n", bandmover->config->getBlockSize());
	snd_output_printf(out, "  hop size     : %lu\n", bandmover->config->getHopSize());
	snd_output_printf(out, "  sample rate  : %zu\n", bandmover->config->getSampleRate());
	if (bandmover->target_band < bandmover->config->getNumBands()) {
		const auto& band = bandmover->config->getBands()[bandmover->target_band];
		snd_output_printf(out, "  band range   : %f Hz - %f Hz\n", band.getStartFreq(), band.getEndFreq());
	}
	snd_pcm_dump_setup(bandmover->ext.pcm, out);
}

static int bandmover_delay(snd_pcm_extplug_t *ext, snd_pcm_sframes_t *delayp)
{
	snd_pcm_bandmover_t *bandmover = (snd_pcm_bandmover_t*)bandmover_ext_to_priv(ext);
	int ret = -1;

	if (delayp) {
		// Calculate total delay from filled samples and available output
		snd_pcm_sframes_t total_delay = bandmover->filled_samples + bandmover->available_output_samples;
		*delayp += 0;
		return 0;
	} else {
		return -1;
	}
}

// Initialize callback structure using C-style initialization
static const snd_pcm_extplug_callback_t bandmover_callback = {
	bandmover_transfer,  /* transfer */
	bandmover_delay,     /* delay */
	bandmover_close,     /* close */
	0,                   /* hw_params */
	0,                   /* hw_free */
	bandmover_dump,      /* dump */
	bandmover_init,      /* init */
	0,                   /* query_chmaps */
	0,                   /* get_chmap */
	0                    /* set_chmap */
};

SND_PCM_PLUGIN_DEFINE_FUNC(bandmover)
{
	snd_config_iterator_t i, next;
	snd_pcm_bandmover_t *bandmover;
	snd_config_t *sconf = NULL;
	unsigned int target_band = 1;  // Default to second band
	int err;

	snd_config_for_each(i, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(i);
		const char *id;
		if (snd_config_get_id(n, &id) < 0)
			continue;
		if (strcmp(id, "comment") == 0 || strcmp(id, "type") == 0 || strcmp(id, "hint") == 0)
			continue;
		if (strcmp(id, "slave") == 0) {
			sconf = n;
			continue;
		}
		if (strcmp(id, "target_band") == 0) {
			long val;
			err = snd_config_get_integer(n, &val);
			if (err < 0) {
				SNDERR("Invalid type for %s", id);
				return err;
			}
			target_band = val;
			continue;
		}
		SNDERR("Unknown field %s", id);
		return -EINVAL;
	}

	if (!sconf) {
		SNDERR("No slave configuration for bandmover pcm");
		return -EINVAL;
	}

	bandmover = new snd_pcm_bandmover_t();
	if (!bandmover)
		return -ENOMEM;

	bandmover->ext.version = SND_PCM_EXTPLUG_VERSION;
	bandmover->ext.name = "Band Mover Plugin";
	bandmover->ext.callback = &bandmover_callback;
	bandmover->ext.private_data = bandmover;
	bandmover->target_band = target_band;
	bandmover->buffers_initialized = false;

	// Create config with default settings
	bandmover->config = new SyncCheckConfig();

	// Create single band processor for the target band
	if (bandmover->target_band < bandmover->config->getNumBands()) {
		bandmover->processor = new BandProcessor(*bandmover->config,
			bandmover->config->getBands()[bandmover->target_band]);
	} else {
		// Handle invalid band index
		SNDERR("Invalid target band index %u", bandmover->target_band);
		return -EINVAL;
	}

	err = snd_pcm_extplug_create(&bandmover->ext, name, root, sconf,
								stream, mode);
	if (err < 0) {
		delete bandmover;
		return err;
	}

	/* this plugin only supports float format */
	snd_pcm_extplug_set_param(&bandmover->ext, SND_PCM_EXTPLUG_HW_FORMAT, SND_PCM_FORMAT_FLOAT_LE);
	snd_pcm_extplug_set_slave_param(&bandmover->ext, SND_PCM_EXTPLUG_HW_FORMAT, SND_PCM_FORMAT_FLOAT_LE);

	/* fix the in/out formats (reference plugin is not able to do any conversion) */
	snd_pcm_extplug_set_param_link(&bandmover->ext, SND_PCM_EXTPLUG_HW_FORMAT, 1);

	*pcmp = bandmover->ext.pcm;
	return 0;
}

} // extern "C"

// Define ALSA plugin symbols
SND_PCM_PLUGIN_SYMBOL(bandmover);

