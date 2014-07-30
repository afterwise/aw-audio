
#include "aw-audio.h"
#include "aw-wav.h"
#include "aw-debug.h"

static size_t wav_render(s16 **output, u64 *frame_offset, const struct audio_waveform *waveform, void *decoder) {
	u64 offset = *frame_offset;
	u64 frame_count = audio_waveform_bufferable(waveform, offset);

	(void) decoder;

	*output = (s16 *) waveform->data + offset * waveform->channel_count;
	*frame_offset = offset + frame_count;

	return frame_count * (waveform->channel_count * sizeof (s16));
}

void audio_waveform_set_wav(struct audio_waveform *waveform, const void *data, size_t size) {
	struct wav_info info;

	(void) size;

	if (wav_parse(&info, data) < 0)
		trespass();

	waveform->render = &wav_render;
	waveform->data = info.blocks;

	waveform->frame_count = info.frame_count;
	waveform->channel_count = info.channel_count;

	check(waveform->channel_count == 1 || waveform->channel_count == 2);
	waveform->native_format = (waveform->channel_count == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;

	waveform->sample_rate = round_f64(info.sample_rate);
}

