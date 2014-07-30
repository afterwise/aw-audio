
#ifndef AW_AUDIO_H
#define AW_AUDIO_H

#if __APPLE__
# include <TargetConditionals.h>
#endif

#if __CELLOS_LV2__
# include <cell/mstream.h>
#elif __ANDROID__
# include <SLES/OpenSLES.h>
#elif _WIN32 || __linux__
# include <AL/al.h>
# include <AL/alc.h>
# include <AL/alext.h>
#elif __APPLE__
# include <OpenAL/al.h>
# include <OpenAL/alc.h>
# if TARGET_OS_IPHONE
#  include <OpenAL/oalMacOSX_OALExtensions.h>
#  include <OpenAL/oalStaticBufferExtension.h>
# elif TARGET_OS_MAC
#  include <OpenAL/MacOSX_OALExtensions.h>
# endif
#endif

#include "aw-arith.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct lma;

#ifdef OPENAL
# if _WIN32 || __linux__
extern PFNALBUFFERDATASTATICPROC alBufferDataStatic;
# elif __APPLE__
extern alBufferDataStaticProcPtr alBufferDataStatic;
# endif
#endif

#define AUDIO_VOICE_COUNT (32)
#define AUDIO_BUFFER_SIZE (16384)

struct audio_waveform {
	size_t (*render)(
		s16 **output, u64 *frame_offset,
		const struct audio_waveform *waveform, void *decoder);
	const void *data;
	u64 frame_count;
	unsigned channel_count;
	unsigned native_format;
	unsigned sample_rate;
};

void audio_init(struct lma *lma);
void audio_end(void);

void audio_update(void);
void audio_late_update(void);

enum {
	AUDIO_LOOPING = 0x1
};

int audio_play(struct audio_waveform *waveform, int flags, void *decoder);
void audio_stop(int voice_id);

static inline u64 audio_waveform_bufferable(const struct audio_waveform *waveform, u64 offset) {
	return min_u64(
		AUDIO_BUFFER_SIZE / (waveform->channel_count * sizeof (s16)),
		waveform->frame_count - offset);
}

int audio_waveform_set_wav(struct audio_waveform *waveform, const void *data, size_t size);
int audio_waveform_set_caf(struct audio_waveform *waveform, const void *data, size_t size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* AW_AUDIO_H */

