#pragma once
#include <stdbool.h>
#include <stdint.h>

#define AUDIO_VOLUME_MAX 21

typedef struct {
    bool playing;       // PCM stream active (a packet within the last 1.5 s)
    bool muted;
    int volume;         // 0..21
    uint32_t packets;   // PCM packets queued since boot
    uint32_t dropped;   // bytes dropped because the jitter buffer was full
    uint32_t underruns; // times the buffer ran dry mid-stream
} audio_status_t;

// I2S output to the MAX98357A plus the UDP 5005 PCM receiver.
void audio_start(void);
// Sine beep through the same buffer as the PC stream (blocks while it queues).
void audio_tone(int freq_hz, int ms);
void audio_boot_melody(void);
// Drops everything queued and ignores packets still in flight for 500 ms.
void audio_stop(void);
void audio_set_muted(bool muted);
// Clamps to 0..21 and saves to NVS. Returns the applied volume.
int audio_set_volume(int volume);
void audio_get_status(audio_status_t *out);
