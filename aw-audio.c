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

#include "aw-audio.h"
#include "aw-debug.h"
#include "aw-ima.h"
#include "aw-wav.h"

#if __APPLE__
# include <TargetConditionals.h>
#endif

#if __ANDROID__
# include <SLES/OpenSLES.h>
#elif _WIN32
# include <al.h>
# include <alc.h>
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

#include <math.h>

#if _MSC_VER
# include <intrin.h>
#endif

#if __GNUC__
# define _audio_alwaysinline inline __attribute__((always_inline))
# define _audio_unused __attribute__((__unused__))
#elif _MSC_VER
# define _audio_alwaysinline __forceinline
# define _audio_unused
#endif

#if __GNUC__
_audio_alwaysinline _audio_unused
static int _audio_ctz32(uint32_t a) { return __builtin_ctz(a); }
_audio_alwaysinline _audio_unused
static int _audio_ctz64(uint64_t a) { return __builtin_ctzl(a); }
#elif _MSC_VER
_audio_alwaysinline _audio_unused
static int _audio_ctz32(uint32_t a) { unsigned long r; _BitScanForward(&r, a); return (int) r; }
_audio_alwaysinline _audio_unused
static int _audio_ctz64(uint64_t a) { unsigned long r; _BitScanForward64(&r, a); return (int) r; }
#endif

#if _WIN32 || __linux__ || __APPLE__

#ifndef NDEBUG
# define alCheckError() \
	do { \
		int _alError = alGetError(); \
		checkf(_alError == AL_NO_ERROR, "error=0x%08x", _alError); \
	} while (0)
#else
# define alCheckError() ((void) 0)
#endif

#if __APPLE__
static alBufferDataStaticProcPtr _audio_alBufferDataStatic;
#endif

#if __ANDROID__
static SLObjectItf _audio_objects[AUDIO_VOICE_COUNT];
static SLPlayItf _audio_plays[AUDIO_VOICE_COUNT];
static SLBufferQueueItf _audio_queues[AUDIO_VOICE_COUNT];
#else
static ALCdevice *_audio_device;
static ALCcontext *_audio_context;
static unsigned _audio_sources[AUDIO_VOICE_COUNT];
static unsigned _audio_buffers[AUDIO_VOICE_COUNT * AUDIO_BUFFER_COUNT];
#endif

struct audio_voice {
	const struct audio_waveform *waveform;
	uint64_t frame_offset;
	void *user_data;
	const char *debug_name;
	uint8_t flags;
	uint8_t dequeued_buffer_index;
	uint8_t queued_buffer_index;
	uint8_t inuse_buffer_count;
};

struct audio_manager {
	uint64_t available_mask;
	uint64_t started_mask;
};

static struct audio_manager _audio_manager;
static struct audio_voice _audio_voices[AUDIO_VOICE_COUNT];
static uint8_t _audio_memory[AUDIO_MEMORY_SIZE];

static inline void _audio_manager_initialize(struct audio_manager *manager) {
	manager->available_mask = ~0ul;
	manager->started_mask = 0ul;
}

static inline uint64_t _audio_manager_started_voice_mask(struct audio_manager *manager) {
	return ~manager->available_mask & manager->started_mask;
}

static inline uint64_t _audio_manager_unstarted_voice_mask(struct audio_manager *manager) {
	return ~manager->available_mask ^ manager->started_mask;
}

static inline audio_voice_id_t _audio_alloc_voice(struct audio_manager *manager) {
	audio_voice_id_t id = -1;

	if (manager->available_mask != 0) {
		id = _audio_ctz64(manager->available_mask);
		manager->available_mask &= ~(1ull << id);
	}

	return id;
}

static inline void _audio_free_voice(
		struct audio_manager *manager, audio_voice_id_t id) {
	manager->available_mask |= 1ull << id;
	manager->started_mask &= ~(1ull << id);
}

static inline void _audio_start_voice(
		struct audio_manager *manager, audio_voice_id_t id) {
	manager->started_mask |= 1ull << id;
}

static inline void _audio_voice_prepare(
		struct audio_voice *voice, struct audio_waveform *waveform, void *user_data,
		int flags, const char *debug_name) {
	voice->waveform = waveform;
	voice->frame_offset = 0;
	voice->user_data = user_data;
	voice->debug_name = debug_name;
	voice->flags = flags;
}

static inline int64_t _audio_voice_render(struct audio_voice *voice, int16_t **buffer) {
	const struct audio_waveform *waveform = voice->waveform;
	int64_t size;

	if (voice->frame_offset < waveform->frame_count) {
	loop:
		if ((size = waveform->render(
			buffer, &voice->frame_offset, waveform, voice->user_data)) > 0)
			return size;
	} else if ((voice->flags & AUDIO_LOOPING) != 0) {
		voice->frame_offset = 0;
		goto loop;
	}

	return -1;
}

static inline uint64_t _audio_waveform_bufferable(
		const struct audio_waveform *waveform, uint64_t offset) {
	const uint64_t a = AUDIO_BUFFER_SIZE / (waveform->channel_count * sizeof(int16_t));
	const uint64_t b = waveform->frame_count - offset;
	return (a < b) ? a : b;
}

int audio_initialize(void) {
	_audio_device = alcOpenDevice(NULL);
	alCheckError();
	check(_audio_device != NULL);

#if __APPLE__
	_audio_alBufferDataStatic = alcGetProcAddress(_audio_device, "alBufferDataStatic");
	alCheckError();
	check(_audio_alBufferDataStatic != NULL);
#endif

	_audio_context = alcCreateContext(_audio_device, NULL);
	alCheckError();
	check(_audio_context != NULL);

	alcMakeContextCurrent(_audio_context);
	alCheckError();

	alGenSources(AUDIO_VOICE_COUNT, _audio_sources);
	alCheckError();

	alGenBuffers(AUDIO_VOICE_COUNT * AUDIO_BUFFER_COUNT, _audio_buffers);
	alCheckError();

	_audio_manager_initialize(&_audio_manager);
	return 0;
}

void audio_terminate(void) {
	alDeleteSources(AUDIO_VOICE_COUNT, _audio_sources);
	alCheckError();

	alDeleteBuffers(AUDIO_VOICE_COUNT * AUDIO_BUFFER_COUNT, _audio_buffers);
	alCheckError();

	alcMakeContextCurrent(NULL);
	alCheckError();

	alcDestroyContext(_audio_context);
	alCheckError();

	alcCloseDevice(_audio_device);
	alCheckError();
}

bool audio_playing(void) {
	return _audio_manager_started_voice_mask(&_audio_manager) != 0;
}

static void _audio_render_and_queue(
		audio_voice_id_t voice_id, unsigned source, unsigned *buffers,
		struct audio_voice *voice) {
	const size_t offset =
		(voice_id * AUDIO_BUFFER_COUNT +
		voice->queued_buffer_index) * AUDIO_BUFFER_SIZE;
	int16_t *mem = (int16_t *) &_audio_memory[offset];
	int64_t size;

	(void) voice_id;

	if ((size = _audio_voice_render(voice, &mem)) > 0) {
#if __APPLE__
		_audio_alBufferDataStatic(
			buffers[voice->queued_buffer_index], voice->waveform->native_format,
			mem, (ALsizei) size, voice->waveform->sample_rate);
		alCheckError();
#else
		alBufferData(
			buffers[voice->queued_buffer_index], voice->waveform->native_format,
			mem, (ALsizei) size, voice->waveform->sample_rate);
		alCheckError();
#endif

		alSourceQueueBuffers(source, 1, &buffers[voice->queued_buffer_index]);
		alCheckError();

		voice->queued_buffer_index =
			(voice->queued_buffer_index + 1) % AUDIO_BUFFER_COUNT;
		voice->inuse_buffer_count++;

		check(voice->inuse_buffer_count <= AUDIO_BUFFER_COUNT);
	}
}

static void _audio_dequeue(
		audio_voice_id_t voice_id, unsigned source, unsigned *buffers,
		struct audio_voice *voice) {
	int i, value = 0;

	(void) voice_id;

	alGetSourcei(source, AL_BUFFERS_PROCESSED, &value);
	alCheckError();

	for (i = 0; i < value; ++i) {
		alSourceUnqueueBuffers(source, 1, &buffers[voice->dequeued_buffer_index]);
		alCheckError();

		voice->dequeued_buffer_index =
			(voice->dequeued_buffer_index + 1) % AUDIO_BUFFER_COUNT;
		voice->inuse_buffer_count--;

		check(voice->inuse_buffer_count <= AUDIO_BUFFER_COUNT);
	}
}

void audio_update(void) {
	struct audio_voice* voice;
	uint64_t mask = _audio_manager_started_voice_mask(&_audio_manager);
	unsigned i, source, *buffers;
	int value;

	if (mask != 0)
		for (i = 0; i < AUDIO_VOICE_COUNT; ++i) {
			if ((mask & (1ull << i)) == 0)
				continue;

			voice = &_audio_voices[i];
			source = _audio_sources[i];
			buffers = &_audio_buffers[i * AUDIO_BUFFER_COUNT];

			_audio_dequeue(i, source, buffers, voice);

			alGetSourcei(source, AL_SOURCE_STATE, &value);
			alCheckError();

			if (value == AL_PLAYING) {
				if (voice->inuse_buffer_count < AUDIO_BUFFER_COUNT)
					_audio_render_and_queue(i, source, buffers, voice);
			} else if (value == AL_STOPPED) {
				if ((voice->flags & AUDIO_RELEASE) != 0)
					_audio_free_voice(&_audio_manager, i);
			}
		}

	mask = _audio_manager_unstarted_voice_mask(&_audio_manager);

	if (mask != 0)
		for (i = 0; i < AUDIO_VOICE_COUNT; ++i) {
			if ((mask & (1ull << i)) == 0)
				continue;

			_audio_start_voice(&_audio_manager, i);

			voice = &_audio_voices[i];
			source = _audio_sources[i];
			buffers = &_audio_buffers[i * AUDIO_BUFFER_COUNT];

			_audio_dequeue(i, source, buffers, voice);
			check(voice->inuse_buffer_count == 0);

			_audio_render_and_queue(i, source, buffers, voice);
			_audio_render_and_queue(i, source, buffers, voice);

			alSourcePlay(source);
			alCheckError();
		}
}

audio_voice_id_t audio_play(
		struct audio_waveform *waveform, void *decoder, int flags,
		const char *debug_name) {
	struct audio_voice *voice;
	audio_voice_id_t voice_id;
	unsigned source;
	float zero[3] = {0.f, 0.f, 0.f};

	if ((voice_id = _audio_alloc_voice(&_audio_manager)) < 0)
		return voice_id;

	voice = &_audio_voices[voice_id];
	source = _audio_sources[voice_id];

	alSourcef(source, AL_PITCH, 1.0f);
	alCheckError();

	alSourcef(source, AL_GAIN, 0.6f);
	alCheckError();

	alSourcefv(source, AL_POSITION, zero);
	alCheckError();

	alSourcefv(source, AL_VELOCITY, zero);
	alCheckError();

	alSourcei(source, AL_SOURCE_RELATIVE, AL_TRUE);
	alCheckError();

	alSourcei(source, AL_LOOPING, AL_FALSE);
	alCheckError();

	_audio_voice_prepare(voice, waveform, decoder, flags, debug_name);
	return voice_id;
}

void audio_stop(audio_voice_id_t voice_id) {
	struct audio_voice* voice;
	unsigned source;

	voice = &_audio_voices[voice_id];
	source = _audio_sources[voice_id];

	voice->flags |= AUDIO_RELEASE;

	alSourceStop(_audio_sources[voice_id]);
	alCheckError();
}

static int64_t _ima_render(
		int16_t **output, uint64_t *frame_offset,
		const struct audio_waveform *waveform, void *decoder) {
	const uint64_t offset = *frame_offset;
	const uint64_t frame_count = _audio_waveform_bufferable(waveform, offset);

	ima_decode(
		*output, offset, frame_count, waveform->data, waveform->channel_count, decoder);
	*frame_offset = offset + frame_count;

	const uint64_t frame_size = (uint64_t) waveform->channel_count * sizeof(int16_t);
	return (int64_t) (frame_count * frame_size);
}

int audio_waveform_prepare_ima(
		struct audio_waveform *waveform, const void *data, size_t size) {
	struct ima_info info;
	int err;

	(void) size;

	if ((err = ima_parse(&info, data)) < 0)
		return err;

	waveform->render = &_ima_render;
	waveform->data = info.blocks;

	waveform->frame_count = info.frame_count;
	waveform->channel_count = info.channel_count;

	check(waveform->channel_count == 1 || waveform->channel_count == 2);
	waveform->native_format =
		(waveform->channel_count == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;

	waveform->sample_rate = (unsigned) lround(info.sample_rate);

	return 0;
}

static int64_t _wav_render(
		int16_t **output, uint64_t *frame_offset,
		const struct audio_waveform *waveform, void *decoder) {
	const uint64_t offset = *frame_offset;
	const uint64_t frame_count = _audio_waveform_bufferable(waveform, offset);

	(void) decoder;

	*output = (int16_t *) waveform->data + offset * waveform->channel_count;
	*frame_offset = offset + frame_count;

	const uint64_t frame_size = (uint64_t) waveform->channel_count * sizeof(int16_t);
	return (int64_t) (frame_count * frame_size);
}

int audio_waveform_prepare_wav(
		struct audio_waveform *waveform, const void *data, size_t size) {
	struct wav_info info;
	int err;

	(void) size;

	if ((err = wav_parse(&info, data)) < 0)
		return err;

	waveform->render = &_wav_render;
	waveform->data = info.blocks;

	waveform->frame_count = info.frame_count;
	waveform->channel_count = info.channel_count;

	check(waveform->channel_count == 1 || waveform->channel_count == 2);
	waveform->native_format =
		(waveform->channel_count == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;

	waveform->sample_rate = (unsigned) lround(info.sample_rate);

	return 0;
}

#endif /* _WIN32 || __linux__ || __APPLE__ */

