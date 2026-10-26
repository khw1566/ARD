#pragma once

#include "board.h"
#include "lvgl.h"

namespace ui_layout {
constexpr int kBarHeight = 72;
constexpr int kTabBarHeight = 56;
constexpr int kPagePadding = 12;
constexpr int kPageWidth = LCD_H_RES - 2 * kPagePadding;
constexpr int kPageHeight = LCD_V_RES - kBarHeight - kTabBarHeight - 2 * kPagePadding;

// Touch drags belong to controls. Pages change only through the tab buttons.
// Programmatic tab selection still uses the tabview's internal scroll offset.
inline void lockScroll(lv_obj_t *obj) {
    lv_obj_remove_flag(obj, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN |
                           LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM |
                           LV_OBJ_FLAG_SCROLL_ON_FOCUS | LV_OBJ_FLAG_GESTURE_BUBBLE));
    lv_obj_set_scroll_dir(obj, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

inline void page(lv_obj_t *obj, int padding = kPagePadding) {
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, padding, 0);
    lockScroll(obj);
}

inline void tabview(lv_obj_t *obj) {
    page(obj, 0);
    lv_obj_set_style_pad_gap(obj, 0, 0);
    lv_obj_t *content = lv_tabview_get_content(obj);
    page(content, 0);
    lv_obj_set_style_pad_gap(content, 0, 0);
    page(lv_tabview_get_tab_bar(obj), 0);
}
} // namespace ui_layout
