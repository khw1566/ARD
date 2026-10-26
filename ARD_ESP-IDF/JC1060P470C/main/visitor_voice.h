#pragma once
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
enum class VisitorVoice {
    Welcome, Touch, Drive, Rail, Camera, Voice, Settings, Stop, Rearm,
    Offline, Online, SortingStart, SortingDone, Safety, Count
};
// Assets: SD /ard/voice/{ko,en}/*.wav, PCM16 mono 16000 Hz, no embedded audio.
esp_err_t visitor_voice_start();
void visitor_voice_request(VisitorVoice id, bool korean, bool preview = false);
void visitor_voice_cancel();
void visitor_voice_print_status();
bool visitor_voice_sd_ready();
const char *visitor_voice_name(VisitorVoice id);
esp_err_t visitor_voice_store(VisitorVoice id, bool korean, const uint8_t *wav, size_t length,
                              const char *expectedSha256, char digest[65]);
esp_err_t visitor_voice_hash(VisitorVoice id, bool korean, size_t *length, char digest[65]);
