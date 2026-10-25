#pragma once
// Sample-count based capture policy, independent of the SDK for host tests.
#include <stdbool.h>
#include <stdint.h>

typedef enum { VOICE_SILENCE, VOICE_START, VOICE_KEEP, VOICE_FINISH, VOICE_CANCEL } voice_capture_event_t;
typedef struct {
    bool recording;
    bool was_ptt;
    bool wait_release;
    uint32_t samples;
    uint32_t quiet;
    uint32_t loud;
    uint32_t voiced;
} voice_capture_t;

// VAD only (PTT is an explicit request). At 16 kHz: speech must be loud for 200 ms
// to start, so taps, clicks and amplifier pops do not open a recording, and a clip
// needs 300 ms of speech in total or it is discarded instead of uploaded.
#define VOICE_VAD_START_SAMPLES 3200u
#define VOICE_VAD_MIN_VOICED_SAMPLES 4800u

static inline voice_capture_event_t voice_capture_step(voice_capture_t *s, bool permitted,
        bool vad, bool ptt, bool speech, uint32_t frame, uint32_t maximum, uint32_t silence) {
    bool pressed = ptt && !s->was_ptt;
    s->was_ptt = ptt;
    if (!ptt) s->wait_release = false;
    if (!permitted) {
        bool active = s->recording;
        s->recording = false;
        s->samples = s->quiet = s->loud = s->voiced = 0;
        // Holding TALK across an interruption must not restart a recording.
        if (ptt) s->wait_release = true;
        return active ? VOICE_CANCEL : VOICE_SILENCE;
    }
    if (!s->recording) {
        s->loud = speech ? s->loud + frame : 0;
        if ((!vad && (!pressed || s->wait_release)) || (vad && s->loud < VOICE_VAD_START_SAMPLES))
            return VOICE_SILENCE;
        s->recording = true;
        s->samples = frame;
        s->voiced = speech ? frame : 0;
        s->quiet = 0;
        return VOICE_START;
    }
    s->samples += frame;
    if (speech) s->voiced += frame;
    s->quiet = speech ? 0 : s->quiet + frame;
    if ((!vad && !ptt) || s->samples >= maximum || (vad && s->quiet >= silence)) {
        const bool too_short = vad && s->voiced < VOICE_VAD_MIN_VOICED_SAMPLES;
        s->recording = false;
        s->loud = s->voiced = 0;
        s->wait_release = ptt;
        return too_short ? VOICE_CANCEL : VOICE_FINISH;
    }
    return VOICE_KEEP;
}
