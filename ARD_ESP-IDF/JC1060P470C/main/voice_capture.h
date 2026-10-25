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
} voice_capture_t;

static inline voice_capture_event_t voice_capture_step(voice_capture_t *s, bool permitted,
        bool vad, bool ptt, bool speech, uint32_t frame, uint32_t maximum, uint32_t silence) {
    bool pressed = ptt && !s->was_ptt;
    s->was_ptt = ptt;
    if (!ptt) s->wait_release = false;
    if (!permitted) {
        bool active = s->recording;
        s->recording = false;
        s->samples = s->quiet = s->loud = 0;
        // Holding TALK across an interruption must not restart a recording.
        if (ptt) s->wait_release = true;
        return active ? VOICE_CANCEL : VOICE_SILENCE;
    }
    if (!s->recording) {
        s->loud = speech ? s->loud + frame : 0;
        if ((!vad && (!pressed || s->wait_release)) || (vad && s->loud < 768)) return VOICE_SILENCE;
        s->recording = true;
        s->samples = frame;
        s->quiet = 0;
        return VOICE_START;
    }
    s->samples += frame;
    s->quiet = speech ? 0 : s->quiet + frame;
    if ((!vad && !ptt) || s->samples >= maximum || (vad && s->quiet >= silence)) {
        s->recording = false;
        s->loud = 0;
        s->wait_release = ptt;
        return VOICE_FINISH;
    }
    return VOICE_KEEP;
}
