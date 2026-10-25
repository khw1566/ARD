#pragma once
// UI language (English / Korean) and the fonts that can draw both.
// Korean works by exact-match lookup: lv_label_set_text(_fmt) and textarea
// placeholders are wrapped at link time (main/CMakeLists.txt), so every UI string
// listed in ui_text.cpp is translated without touching program logic that compares
// the same English words. Words passed as %s arguments go through ui_tr() explicitly.
#include "esp_err.h"
#include "lvgl.h"

enum UiLanguage { UI_LANG_EN = 0, UI_LANG_KO = 1 };

// Before any UI object is created: loads the saved language and builds the fonts.
void ui_text_init();
UiLanguage ui_language();
// Saves the choice for the next start; screens are built once, so the caller restarts.
esp_err_t ui_save_language(UiLanguage language);
const char *ui_tr(const char *english);

// Montserrat (layout metrics, Latin, symbols) with Nanum Gothic Hangul as fallback.
extern lv_font_t ard_font_16, ard_font_20, ard_font_28;
