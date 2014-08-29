
#ifndef AW_AUDIO_H
#define AW_AUDIO_H

#include "aw-voice.h"

#ifdef __cplusplus
extern "C" {
#endif

struct lma;

#define AUDIO_VOICE_COUNT (32)
#define AUDIO_BUFFER_COUNT (2)
#define AUDIO_BUFFER_SIZE (16384)

void audio_init(struct lma *lma);
void audio_end(void);

void audio_update(void);
void audio_late_update(void);

int audio_play(struct voice_waveform *waveform, void *decoder, int voice_flags);
void audio_stop(int voice_id);

static inline u64 audio_waveform_bufferable(const struct voice_waveform *waveform, u64 offset) {
	return min_u64(
		AUDIO_BUFFER_SIZE / (waveform->channel_count * sizeof (s16)),
		waveform->frame_count - offset);
}

int audio_setwavdata(struct voice_waveform *waveform, const void *data, size_t size);
int audio_setimadata(struct voice_waveform *waveform, const void *data, size_t size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* AW_AUDIO_H */

