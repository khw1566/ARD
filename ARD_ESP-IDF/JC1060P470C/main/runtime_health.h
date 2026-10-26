#pragma once
#include "esp_err.h"
enum class HealthBeat { Ui, Audio };
esp_err_t runtime_health_start();
void runtime_health_beat(HealthBeat beat);
void runtime_health_audio_enabled(bool enabled);
void runtime_health_print();
