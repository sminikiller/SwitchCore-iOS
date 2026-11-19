// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct SCSession SCSession;
typedef enum { SC_SWITCH_1, SC_SWITCH_2_RESEARCH } SCBackend;
typedef enum { SC_EMPTY, SC_RUNNING, SC_EXITED, SC_FAULT } SCState;
typedef struct {
    SCState state;
    uint64_t instructions;
    uint64_t guest_bytes;
    uint64_t frames;
    uint64_t pc;
} SCStats;
// Calls on a session are serialized by its owner. Frame/PCM pointers live only
// inside their synchronous callback. Audio is interleaved stereo, signed 16-bit, 48 kHz.
typedef void (*SCFrameSink)(void *, const uint32_t *, uint32_t, uint32_t);
typedef void (*SCAudioSink)(void *, const int16_t *, size_t);
SCSession *sc_create(void);
void sc_destroy(SCSession *session);
bool sc_load(SCSession *, SCBackend, const char *nro_path, const char *title_directory);
void sc_cache(SCSession *, bool enabled);
void sc_pad(SCSession *, uint64_t buttons, int32_t lx, int32_t ly, int32_t rx, int32_t ry);
void sc_touch(SCSession *, bool down, uint32_t x, uint32_t y);
void sc_audio(SCSession *, SCAudioSink sink, void *context);
SCStats sc_step(SCSession *, uint32_t budget);
bool sc_frame(SCSession *, uint64_t after, SCFrameSink sink, void *context, uint64_t *number);
const char *sc_message(SCSession *);
const char *sc_title(SCSession *);
const char *sc_log(SCSession *);
#ifdef __cplusplus
}
#endif

