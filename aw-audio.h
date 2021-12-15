/* vim: set ts=4 sw=4 noet : */
/*
   Copyright (c) 2014-2021 Malte Hildingsson, malte (at) afterwi.se

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
   THE SOFTWARE.
 */

#ifndef AW_AUDIO_H
#define AW_AUDIO_H

#include <stddef.h>

#if !_MSC_VER || _MSC_VER >= 1800
# include <stdbool.h>
#endif
#if !_MSC_VER || _MSC_VER >= 1600
# include <stdint.h>
#endif

#if defined(_audio_dllexport)
# if _MSC_VER
#  define _audio_api extern __declspec(dllexport)
# elif __GNUC__
#  define _audio_api __attribute__((visibility("default"))) extern
# endif
#elif defined(_audio_dllimport)
# if _MSC_VER
#  define _audio_api extern __declspec(dllimport)
# endif
#endif
#ifndef _audio_api
# define _audio_api extern
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_VOICE_COUNT (32)
#define AUDIO_BUFFER_COUNT (2)
#define AUDIO_BUFFER_SIZE (16384)
#define AUDIO_MEMORY_SIZE \
	(AUDIO_VOICE_COUNT * AUDIO_BUFFER_COUNT * AUDIO_BUFFER_SIZE)

struct audio_waveform {
	int64_t (*render)(
		int16_t **output, uint64_t *frame_offset,
		const struct audio_waveform *waveform, void *user_data);
	uint64_t frame_count;
	const void *data;
	unsigned channel_count;
	unsigned native_format;
	unsigned sample_rate;
};

typedef int audio_voice_id_t;

_audio_api int audio_initialize(void);
_audio_api void audio_terminate(void);

_audio_api bool audio_playing(void);

_audio_api void audio_update(void);

enum {
	AUDIO_RELEASE = 0x1,
	AUDIO_LOOPING = 0x2,
};

_audio_api audio_voice_id_t audio_play(
	struct audio_waveform *waveform, void *decoder, int flags,
	const char *debug_name);

_audio_api void audio_stop(audio_voice_id_t voice_id);

_audio_api int audio_waveform_prepare_ima(
	struct audio_waveform *waveform, const void *data, size_t size);

_audio_api int audio_waveform_prepare_wav(
	struct audio_waveform *waveform, const void *data, size_t size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* AW_AUDIO_H */

