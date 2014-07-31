
#include "aw-audio.h"
#include "aw-debug.h"
#include "aw-lma.h"
#include <string.h>

#ifndef NDEBUG
# define alCheckError() \
        do { \
                int _alError = alGetError(); \
                checkf(_alError == AL_NO_ERROR, "error=0x%08x", _alError); \
        } while (0)
#else
# define alCheckError() ((void) 0)
#endif

#if _WIN32 || __linux__
PFNALBUFFERDATASTATICPROC alBufferDataStatic;
#elif __APPLE__
alBufferDataStaticProcPtr alBufferDataStatic;
#endif

struct audio_voice {
	const struct audio_waveform *waveform;
	void *decoder;
	u64 frame_offset;
	unsigned buffer_index;
	int flags;
};

struct audio {
#ifdef OPENAL
	ALCdevice *device;
	ALCcontext *context;
#elif __ANDROID__
	unsigned active;
	SLObjectItf object;
#elif __CELLOS_LV2__
	uintptr_t thread;
	void *memory;
	void *mp3_memory;
	unsigned port;
#endif

	s16 *buffers;
	unsigned free_mask;
	unsigned play_mask;
	struct audio_voice voices[AUDIO_VOICE_COUNT];

#ifdef OPENAL
	unsigned sources[AUDIO_VOICE_COUNT];
	unsigned names[AUDIO_VOICE_COUNT << 1];
#elif __ANDROID__
	SLObjectItf objects[AUDIO_VOICE_COUNT];
	SLPlayItf plays[AUDIO_VOICE_COUNT];
	SLBufferQueueItf queues[AUDIO_VOICE_COUNT];
#elif __CELLOS_LV2__
	CellMSInfo infos[AUDIO_VOICE_COUNT];
#endif
};

static struct audio *audio;

void audio_init(struct lma *lma) {
	audio = lma_alloc(lma, sizeof (struct audio));
	memset(audio, 0, sizeof (struct audio));

	audio->device = alcOpenDevice(NULL);
	alCheckError();
	check(audio->device != NULL);

	alBufferDataStatic = alcGetProcAddress(audio->device, "alBufferDataStatic");
	alCheckError();
	check(alBufferDataStatic != NULL);

	audio->context = alcCreateContext(audio->device, NULL);
	alCheckError();
	check(audio->context != NULL);

	alcMakeContextCurrent(audio->context);
	alCheckError();

	audio->buffers = lma_alloc(lma, AUDIO_VOICE_COUNT * AUDIO_BUFFER_SIZE * 2);
	check(audio->buffers != NULL);

	audio->free_mask = ~0u;

	alGenSources(AUDIO_VOICE_COUNT, audio->sources);
	alCheckError();

	alGenBuffers(AUDIO_VOICE_COUNT * 2, audio->names);
	alCheckError();
}

void audio_end(void) {
	alDeleteSources(AUDIO_VOICE_COUNT, audio->sources);
	alCheckError();

	alDeleteBuffers(AUDIO_VOICE_COUNT * 2, audio->names);
	alCheckError();

	alcMakeContextCurrent(NULL);
	alCheckError();

	alcDestroyContext(audio->context);
	alCheckError();

	alcCloseDevice(audio->device);
	alCheckError();
}

static void queue_voice(
		unsigned voice_id, unsigned source, unsigned name, s16 *buffers,
		struct audio_voice *voice) {
	const struct audio_waveform *waveform = voice->waveform;
	s16 *buffer = &buffers[(voice_id * 2 + voice->buffer_index) * AUDIO_BUFFER_SIZE];
	size_t size;

	if (voice->frame_offset < waveform->frame_count) {
loop:
		if ((size = waveform->render(&buffer, &voice->frame_offset, waveform, voice->decoder)) > 0) {
			alBufferDataStatic(
				name, waveform->native_format,
				buffer, size, waveform->sample_rate);
			alCheckError();

			alSourceQueueBuffers(source, 1, &name);
			alCheckError();

			voice->buffer_index ^= 1;
		}
	} else if ((voice->flags & AUDIO_LOOPING) != 0) {
		voice->frame_offset = 0;
		goto loop;
	}
}

void audio_update(void) {
	unsigned i, source, name;
	unsigned update_mask = ~audio->free_mask & audio->play_mask;
	int value;

	if (update_mask != 0)
		for (i = 0; i < AUDIO_VOICE_COUNT; ++i) {
			if ((update_mask & (1 << i)) == 0)
				continue;

			source = audio->sources[i];

			alGetSourcei(source, AL_BUFFERS_PROCESSED, &value);
			alCheckError();

			while (value-- > 0) {
				alSourceUnqueueBuffers(source, 1, &name);
				alCheckError();

				queue_voice(i, source, name, audio->buffers, &audio->voices[i]);
			}

			alGetSourcei(source, AL_SOURCE_STATE, &value);
			alCheckError();

			if (value == AL_STOPPED) {
				audio->free_mask |= 1 << i;
				audio->play_mask &= ~(1 << i);
			}
		}
}

void audio_late_update(void) {
	struct audio_voice *voice;
	unsigned update_mask = ~audio->free_mask ^ audio->play_mask;
	unsigned i, source, *names;

	if (update_mask != 0)
		for (i = 0; i < AUDIO_VOICE_COUNT; ++i) {
			if ((update_mask & (1 << i)) == 0)
				continue;

			audio->play_mask |= 1 << i;

			voice = &audio->voices[i];
			source = audio->sources[i];
			names = &audio->names[i* 2];

			queue_voice(i, source, names[voice->buffer_index], audio->buffers, voice);
			queue_voice(i, source, names[voice->buffer_index], audio->buffers, voice);

			alSourcePlay(source);
			alCheckError();
		}
}

int audio_play(struct audio_waveform *waveform, int flags, void *decoder) {
	struct audio_voice *voice;
	unsigned source, *names;
	int voice_id;
	float zero[3];

	if (audio->free_mask == 0)
		return -1;

	voice_id = ctz_u32(audio->free_mask);
	audio->free_mask &= ~(1 << voice_id);

	voice = &audio->voices[voice_id];
	source = audio->sources[voice_id];
	names = &audio->names[voice_id * 2];

	memset(zero, 0, sizeof zero);

	alSourcef(source, AL_PITCH, 1.0f);
	alCheckError();

	alSourcef(source, AL_GAIN, 1.0f);
	alCheckError();

	alSourcefv(source, AL_POSITION, zero);
	alCheckError();

	alSourcefv(source, AL_VELOCITY, zero);
	alCheckError();

	alSourcei(source, AL_SOURCE_RELATIVE, AL_TRUE);
	alCheckError();

	alSourcei(source, AL_LOOPING, AL_FALSE);
	alCheckError();

	voice->waveform = waveform;
	voice->decoder = decoder;
	voice->frame_offset = 0;
	voice->flags = flags;

	return voice_id;
}

void audio_stop(int voice_id) {
	alSourceStop(audio->sources[voice_id]);
	alCheckError();

	audio->free_mask |= 1 << voice_id;
	audio->play_mask &= ~(1 << voice_id);
}

