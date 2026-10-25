#include <assert.h>
#include <stdio.h>
#include "../main/voice_capture.h"

static voice_capture_event_t tick(voice_capture_t *s, bool allowed, bool vad, bool held, bool speech) {
    return voice_capture_step(s, allowed, vad, held, speech, 256, 16000, 12000);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    voice_capture_t vad = {0};
    assert(tick(&vad, true, true, false, true) == VOICE_SILENCE);
    assert(tick(&vad, true, true, false, false) == VOICE_SILENCE);
    assert(tick(&vad, true, true, false, true) == VOICE_SILENCE);
    assert(tick(&vad, true, true, false, true) == VOICE_SILENCE);
    assert(tick(&vad, true, true, false, true) == VOICE_START);
    for (int i = 0; i < 46; ++i) assert(tick(&vad, true, true, false, false) == VOICE_KEEP);
    assert(tick(&vad, true, true, false, false) == VOICE_FINISH);
    assert(!vad.recording);

    voice_capture_t ptt = {0};
    assert(tick(&ptt, true, false, false, false) == VOICE_SILENCE);
    assert(tick(&ptt, true, false, true, false) == VOICE_START);
    assert(tick(&ptt, true, false, true, true) == VOICE_KEEP);
    assert(tick(&ptt, true, false, false, false) == VOICE_FINISH);
    assert(!ptt.recording);

    voice_capture_t cancel = {0};
    assert(tick(&cancel, true, false, true, true) == VOICE_START);
    assert(tick(&cancel, false, false, true, true) == VOICE_CANCEL);
    assert(tick(&cancel, true, false, true, true) == VOICE_SILENCE);
    assert(tick(&cancel, true, false, false, false) == VOICE_SILENCE);
    assert(tick(&cancel, true, false, true, true) == VOICE_START);

    voice_capture_t limit = {0};
    assert(tick(&limit, true, false, true, true) == VOICE_START);
    int frames = 1;
    while (tick(&limit, true, false, true, true) != VOICE_FINISH) assert(++frames < 64);
    assert(limit.samples >= 16000 && limit.samples < 16256);
    assert(tick(&limit, true, false, true, true) == VOICE_SILENCE);
    assert(tick(&limit, true, false, false, false) == VOICE_SILENCE);
    assert(tick(&limit, true, false, true, true) == VOICE_START);
    puts("Voice capture: VAD, silence, PTT, interruption, duration limit and release latch passed");
    return 0;
}
