#pragma once
#include <stddef.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

struct OnboardAudioStatus {
    bool ready, micEnabled, selected, vad, recording, playing, uploading, waiting, muted;
    int volume, peak;
    uint32_t rxPackets, droppedPackets, uploadedClips, uploadErrors;
    char error[64];
};

// ES8311 full duplex: 16 kHz / 16-bit stereo I2S; PC transport is mono PCM16LE.
esp_err_t onboard_audio_start(i2c_master_bus_handle_t bus);
OnboardAudioStatus onboard_audio_status();
void onboard_audio_pc_state(bool selected, bool vad, bool busy, uint32_t completedJob, uint32_t session,
                            int volume, int threshold, const char *serverIp);
void onboard_audio_set_ptt(bool held);
void onboard_audio_set_mic(bool enabled);
// 0..21, saved in NVS. Wins over the PC's pushed value for a few seconds so the
// PC can store the same change (send "vol=N" to the PC when this board is selected).
void onboard_audio_set_volume(int volume);
void onboard_audio_stop();
void onboard_audio_boot();
void onboard_audio_test();
// Recognized audio commands never reach the motor command parser. "stop" stays E-STOP.
bool onboard_audio_is_command(const char *command);
bool onboard_audio_command(const char *command);
