
#include "aw-audio.h"
#include "aw-ima.h"
#include "aw-debug.h"

static size_t ima_render(
		s16 **output, u64 *frame_offset, const struct audio_waveform *waveform, void *decoder) {
	unsigned frame_count = audio_waveform_bufferable(waveform, *frame_offset);

	ima_decode(*output, *frame_offset, frame_count, waveform->data, waveform->channel_count, decoder);
	*frame_offset += frame_count;

	return frame_count * (waveform->channel_count * sizeof (u16));
}

int audio_waveform_set_caf(struct audio_waveform *waveform, const void *data, size_t size) {
	struct ima_info info;
	int err;

	(void) size;

	if ((err = ima_parse(&info, data)) < 0)
		return err;

	waveform->render = &ima_render;
	waveform->data = info.blocks;

	waveform->frame_count = info.frame_count;
	waveform->channel_count = info.channel_count;

	check(waveform->channel_count == 1 || waveform->channel_count == 2);
	waveform->native_format = (waveform->channel_count == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;

	waveform->sample_rate = round_f64(info.sample_rate);

	return 0;
}

