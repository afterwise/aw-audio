
#include "aw-audio.h"
#include "aw-debug.h"
#include "aw-ima.h"
#include "aw-lma.h"
#include "aw-wav.h"

#if __APPLE__
# include <TargetConditionals.h>
#endif

#if __ANDROID__
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

#include <string.h>

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

#if _WIN32 || __linux__
static PFNALBUFFERDATASTATICPROC alBufferDataStatic;
#elif __APPLE__
static alBufferDataStaticProcPtr alBufferDataStatic;
#endif

#if __ANDROID__
static unsigned active;
static SLObjectItf object;
static SLObjectItf objects[AUDIO_VOICE_COUNT];
static SLPlayItf plays[AUDIO_VOICE_COUNT];
static SLBufferQueueItf queues[AUDIO_VOICE_COUNT];
#else
static ALCdevice *device;
static ALCcontext *context;
static unsigned sources[AUDIO_VOICE_COUNT];
static unsigned names[AUDIO_VOICE_COUNT * AUDIO_BUFFER_COUNT];
#endif

static struct voice_manager voice_manager;
static struct voice voices[AUDIO_VOICE_COUNT];
static int indices[AUDIO_VOICE_COUNT];

static s16 *buffers;

void audio_init(struct lma *lma) {
	device = alcOpenDevice(NULL);
	alCheckError();
	check(device != NULL);

	alBufferDataStatic = alcGetProcAddress(device, "alBufferDataStatic");
	alCheckError();
	check(alBufferDataStatic != NULL);

	context = alcCreateContext(device, NULL);
	alCheckError();
	check(context != NULL);

	alcMakeContextCurrent(context);
	alCheckError();

	alGenSources(AUDIO_VOICE_COUNT, sources);
	alCheckError();

	alGenBuffers(AUDIO_VOICE_COUNT * AUDIO_BUFFER_COUNT, names);
	alCheckError();

        buffers = lma_alloc(lma, AUDIO_VOICE_COUNT * AUDIO_BUFFER_COUNT * AUDIO_BUFFER_SIZE);
        check(buffers != NULL);
}

void audio_end(void) {
	alDeleteSources(AUDIO_VOICE_COUNT, sources);
	alCheckError();

	alDeleteBuffers(AUDIO_VOICE_COUNT * AUDIO_BUFFER_COUNT, names);
	alCheckError();

	alcMakeContextCurrent(NULL);
	alCheckError();

	alcDestroyContext(context);
	alCheckError();

	alcCloseDevice(device);
	alCheckError();
}

static void queue_voice(
		unsigned voice_id, unsigned source, unsigned name, s16 *buffers,
		struct voice *voice) {
	int index = indices[voice_id];
	s16 *buffer = &buffers[(voice_id * AUDIO_BUFFER_COUNT + index) * AUDIO_BUFFER_SIZE];
	ssize_t size;

	if ((size = voice_render(voice, &buffer)) > 0) {
		indices[voice_id] = (index + 1) % AUDIO_BUFFER_COUNT;

		alBufferDataStatic(
			name, voice->waveform->native_format,
			buffer, size, voice->waveform->sample_rate);
		alCheckError();

		alSourceQueueBuffers(source, 1, &name);
		alCheckError();
	}
}

void audio_update(void) {
	unsigned i, source, name;
	u32 mask = voice_getopen(&voice_manager);
	int value;

	if (mask != 0)
		for (i = 0; i < AUDIO_VOICE_COUNT; ++i) {
			if ((mask & (1 << i)) == 0)
				continue;

			source = sources[i];

			alGetSourcei(source, AL_BUFFERS_PROCESSED, &value);
			alCheckError();

			while (value-- > 0) {
				alSourceUnqueueBuffers(source, 1, &name);
				alCheckError();

				queue_voice(i, source, name, buffers, &voices[i]);
			}

			alGetSourcei(source, AL_SOURCE_STATE, &value);
			alCheckError();

			if (value == AL_STOPPED)
				voice_close(&voice_manager, 1 << i);
		}
}

void audio_late_update(void) {
	struct voice *voice;
	u32 mask = voice_getunstarted(&voice_manager);
	unsigned i, source, *name;

	if (mask != 0)
		for (i = 0; i < AUDIO_VOICE_COUNT; ++i) {
			if ((mask & (1 << i)) == 0)
				continue;

			voice_start(&voice_manager, 1 << i);

			voice = &voices[i];
			source = sources[i];
			name = &names[i * AUDIO_BUFFER_COUNT];

			queue_voice(i, source, name[indices[i]], buffers, voice);
			queue_voice(i, source, name[indices[i]], buffers, voice);

			alSourcePlay(source);
			alCheckError();
		}
}

int audio_play(struct voice_waveform *waveform, void *decoder, int voice_flags) {
	struct voice *voice;
	unsigned source;
	int voice_id;
	float zero[3];

	if ((voice_id = voice_open(&voice_manager)) < 0)
		return voice_id;

	voice = &voices[voice_id];
	source = sources[voice_id];

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

	voice_finalize(voice, waveform, decoder, voice_flags);

	return voice_id;
}

void audio_stop(int voice_id) {
	alSourceStop(sources[voice_id]);
	alCheckError();

	voice_close(&voice_manager, voice_id);
}

static size_t ima_render(
		s16 **output, u64 *frame_offset, const struct voice_waveform *waveform, void *decoder) {
	unsigned frame_count = audio_waveform_bufferable(waveform, *frame_offset);

	ima_decode(*output, *frame_offset, frame_count, waveform->data, waveform->channel_count, decoder);
	*frame_offset += frame_count;

	return frame_count * (waveform->channel_count * sizeof (u16));
}

int audio_waveform_set_caf(struct voice_waveform *waveform, const void *data, size_t size) {
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

static size_t wav_render(s16 **output, u64 *frame_offset, const struct voice_waveform *waveform, void *decoder) {
	u64 offset = *frame_offset;
	u64 frame_count = audio_waveform_bufferable(waveform, offset);

	(void) decoder;

	*output = (s16 *) waveform->data + offset * waveform->channel_count;
	*frame_offset = offset + frame_count;

	return frame_count * (waveform->channel_count * sizeof (s16));
}

int audio_waveform_set_wav(struct voice_waveform *waveform, const void *data, size_t size) {
	struct wav_info info;
	int err;

	(void) size;

	if ((err = wav_parse(&info, data)) < 0)
		return err;

	waveform->render = &wav_render;
	waveform->data = info.blocks;

	waveform->frame_count = info.frame_count;
	waveform->channel_count = info.channel_count;

	check(waveform->channel_count == 1 || waveform->channel_count == 2);
	waveform->native_format = (waveform->channel_count == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;

	waveform->sample_rate = round_f64(info.sample_rate);

	return 0;
}

#endif /* _WIN32 || __linux__ || __APPLE__ */

