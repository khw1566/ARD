#include <assert.h>
#include <stdio.h>
#include "../main/voice_capture.h"

static voice_capture_event_t tick(voice_capture_t *s, bool allowed, bool vad, bool held, bool speech) {
    return voice_capture_step(s, allowed, vad, held, speech, 256, 16000, 12000);
}

// VAD cases need room for 200 ms + 300 ms of speech and 0.75 s of silence under the cap.
static voice_capture_event_t vtick(voice_capture_t *s, bool speech) {
    return voice_capture_step(s, true, true, false, speech, 256, 32000, 12000);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    voice_capture_t vad = {0};
    // 200 ms (13 frames of 256) of continuous sound starts VAD; a gap restarts the count.
    for (int i = 0; i < 5; ++i) assert(vtick(&vad, true) == VOICE_SILENCE);
    assert(vtick(&vad, false) == VOICE_SILENCE);
    for (int i = 0; i < 12; ++i) assert(vtick(&vad, true) == VOICE_SILENCE);
    assert(vtick(&vad, true) == VOICE_START);
    // 19 loud frames in total (4864 samples) clear the 300 ms speech minimum.
    for (int i = 0; i < 18; ++i) assert(vtick(&vad, true) == VOICE_KEEP);
    for (int i = 0; i < 46; ++i) assert(vtick(&vad, false) == VOICE_KEEP);
    assert(vtick(&vad, false) == VOICE_FINISH);
    assert(!vad.recording);

    // Under 300 ms of speech in total (a knock, a short noise burst) is discarded.
    voice_capture_t noise = {0};
    for (int i = 0; i < 12; ++i) assert(vtick(&noise, true) == VOICE_SILENCE);
    assert(vtick(&noise, true) == VOICE_START);
    for (int i = 0; i < 46; ++i) assert(vtick(&noise, false) == VOICE_KEEP);
    assert(vtick(&noise, false) == VOICE_CANCEL);
    assert(!noise.recording);

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
    puts("Voice capture: VAD start/minimum speech, silence, PTT, interruption, duration limit and release latch passed");
    return 0;
}
