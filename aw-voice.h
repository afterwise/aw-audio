
#ifndef AW_VOICE_H
#define AW_VOICE_H

#include "aw-arith.h"
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct voice_waveform {
	size_t (*render)(
		s16 **output, u64 *frame_offset,
		const struct voice_waveform *waveform, void *user_data);
	const void *data;
	u32 frame_count;
	unsigned channel_count;
	unsigned native_format;
	unsigned sample_rate;
};

struct voice {
	const struct voice_waveform *waveform;
	void *user_data;
	u64 frame_offset;
	int flags;
};

struct voice_manager {
	u32 closed;
	u32 started;
};

static inline void voice_init(struct voice_manager *manager) {
	manager->closed = ~0;
	manager->started = 0;
}

static inline u32 voice_getopen(struct voice_manager *manager) {
	return ~manager->closed & manager->started;
}

static inline u32 voice_getunstarted(struct voice_manager *manager) {
	return ~manager->closed ^ manager->started;
}

static inline void voice_start(struct voice_manager *manager, int id) {
	manager->started |= 1 << id;
}

static inline int voice_open(struct voice_manager *manager) {
	int id = -1;

	if (manager->closed != 0) {
		id = ctz_u32(manager->closed);
		manager->closed &= ~(1 << id);
	}

	return id;
}

static inline void voice_close(struct voice_manager *manager, int id) {
	manager->closed |= 1 << id;
	manager->started &= ~(1 << id);
}

enum {
	VOICE_LOOPING = 0x1
};

static inline void voice_finalize(
		struct voice *voice, struct voice_waveform *waveform, void *user_data, int flags) {
	voice->waveform = waveform;
	voice->user_data = user_data;
	voice->frame_offset = 0;
	voice->flags = flags;
}

static inline ssize_t voice_render(struct voice *voice, s16 **buffer) {
	const struct voice_waveform *waveform = voice->waveform;
	ssize_t size;

	if (voice->frame_offset < waveform->frame_count) {
loop:
		if ((size = waveform->render(buffer, &voice->frame_offset, waveform, voice->user_data)) > 0)
			return size;
	} else if ((voice->flags & VOICE_LOOPING) != 0) {
		voice->frame_offset = 0;
		goto loop;
	}

	return -1;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* AW_VOICE_H */

