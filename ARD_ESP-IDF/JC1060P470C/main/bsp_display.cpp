#include "bsp_display.h"
#include <atomic>
#include <cstring>
#include <initializer_list>
#include "board.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_lcd_jd9165.h"
#include "panel_init.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs.h"

static const char *TAG = "bsp";

#ifndef CONFIG_ARD_TOUCH_SWAP_XY
#define CONFIG_ARD_TOUCH_SWAP_XY 0
#endif
#ifndef CONFIG_ARD_TOUCH_MIRROR_X
#define CONFIG_ARD_TOUCH_MIRROR_X 0
#endif
#ifndef CONFIG_ARD_TOUCH_MIRROR_Y
#define CONFIG_ARD_TOUCH_MIRROR_Y 0
#endif


namespace {


constexpr int kDsiLanes = 2;
constexpr float kLaneMbps = 750;
constexpr int kDsiPhyLdoChannel = 3;     // Espressif P4 reference design; assumed for this board
constexpr int kDsiPhyLdoMv = 2500;
#if CONFIG_ARD_LCD_SYNC_FULL_REFRESH
constexpr bool kSyncFullRefresh = true;
#else
constexpr bool kSyncFullRefresh = false;
#endif
constexpr int kDrawLines = kSyncFullRefresh ? LCD_V_RES : 100;
constexpr ledc_timer_t kBlTimer = LEDC_TIMER_1;     // timer 0 / channel 0 belong to the buzzer
constexpr ledc_channel_t kBlChannel = LEDC_CHANNEL_1;

esp_lcd_panel_handle_t panel;
esp_lcd_touch_handle_t touch;
SemaphoreHandle_t lvglMutex;
std::atomic<int> backlight{80};

bool IRAM_ATTR onColorTransDone(esp_lcd_panel_handle_t, esp_lcd_dpi_panel_event_data_t *, void *ctx) {
    lv_display_flush_ready(static_cast<lv_display_t *>(ctx));
    return false;
}

void flush(lv_display_t *disp, const lv_area_t *area, uint8_t *pixels) {
    const esp_err_t err = esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1,
                                                  area->x2 + 1, area->y2 + 1, pixels);
    // CPU copies finish before draw_bitmap returns. Only the DMA2D path uses
    // onColorTransDone; signal exactly once, and never deadlock LVGL on an error.
    if (kSyncFullRefresh || err != ESP_OK) lv_display_flush_ready(disp);
    if (err != ESP_OK) ESP_LOGE(TAG, "LCD flush failed: %s", esp_err_to_name(err));
}

void touchRead(lv_indev_t *, lv_indev_data_t *data) {
    data->state = LV_INDEV_STATE_RELEASED;
    if (!touch || esp_lcd_touch_read_data(touch) != ESP_OK) return;
    esp_lcd_touch_point_data_t point[1];
    uint8_t count = 0;
    if (esp_lcd_touch_get_data(touch, point, &count, 1) == ESP_OK && count) {
        data->point.x = point[0].x;
        data->point.y = point[0].y;
        data->state = LV_INDEV_STATE_PRESSED;
    }
}

uint32_t tickMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

void lvglTask(void *) {
    for (;;) {
        uint32_t wait = 10;
        if (xSemaphoreTakeRecursive(lvglMutex, portMAX_DELAY) == pdTRUE) {
            wait = lv_timer_handler();
            xSemaphoreGiveRecursive(lvglMutex);
        }
        if (wait < 5) wait = 5;
        if (wait > 30) wait = 30;
        vTaskDelay(pdMS_TO_TICKS(wait));
    }
}

esp_err_t backlightInit() {
    ledc_timer_config_t timer = {};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = LEDC_TIMER_10_BIT;
    timer.timer_num = kBlTimer;
    timer.freq_hz = 20000; // above audible range
    timer.clk_cfg = LEDC_AUTO_CLK;
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "bl timer");
    ledc_channel_config_t channel = {};
    channel.gpio_num = PIN_BACKLIGHT;
    channel.speed_mode = LEDC_LOW_SPEED_MODE;
    channel.channel = kBlChannel;
    channel.timer_sel = kBlTimer;
#if CONFIG_ARD_BACKLIGHT_ACTIVE_LOW
    channel.flags.output_invert = 1;
#endif
    channel.duty = 0; // dark until the first frame is drawn
    return ledc_channel_config(&channel);
}

esp_err_t touchInit(i2c_master_bus_handle_t bus) {
    // The GT911 address depends on its INT level at reset; we do not drive reset, so probe both.
    uint8_t address = 0;
    for (uint8_t candidate : {ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP}) {
        if (i2c_master_probe(bus, candidate, 50) == ESP_OK) { address = candidate; break; }
    }
    ESP_RETURN_ON_FALSE(address, ESP_ERR_NOT_FOUND, TAG, "GT911 not found at 0x5D/0x14");
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_i2c_config_t ioConfig = {}; // ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG() is C-only
    ioConfig.dev_addr = address;
    ioConfig.control_phase_bytes = 1;
    ioConfig.dc_bit_offset = 0;
    ioConfig.lcd_cmd_bits = 16;
    ioConfig.flags.disable_control_phase = 1;
    ioConfig.scl_speed_hz = 400000;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c_v2(bus, &ioConfig, &io), TAG, "touch io");
    esp_lcd_touch_config_t config = {};
    config.x_max = LCD_H_RES;
    config.y_max = LCD_V_RES;
    // Reset/INT are left alone: community notes say the touch reset line is shared with other
    // functions on this board (GPIO22 or GPIO23 depending on the source).
    config.rst_gpio_num = GPIO_NUM_NC;
    config.int_gpio_num = GPIO_NUM_NC;
    config.flags.swap_xy = CONFIG_ARD_TOUCH_SWAP_XY;
    config.flags.mirror_x = CONFIG_ARD_TOUCH_MIRROR_X;
    config.flags.mirror_y = CONFIG_ARD_TOUCH_MIRROR_Y;
    static esp_lcd_touch_io_gt911_config_t gt911; // the driver keeps this pointer
    gt911.dev_addr = address;
    config.driver_data = &gt911;
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(io, &config, &touch), TAG, "gt911");
    ESP_LOGI(TAG, "GT911 at 0x%02X", address);
    return ESP_OK;
}

#if CONFIG_ARD_LCD_BOOT_TEST
esp_err_t displayBootTest() {
    // The DSI host generates these colors without LVGL, DMA2D or the PSRAM
    // framebuffer. Run before any LVGL task or touch callback can start.
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_set_pattern(panel, MIPI_DSI_PATTERN_BAR_VERTICAL),
                        TAG, "vertical test pattern");
    bsp_backlight_set(backlight.load());
    ESP_LOGI(TAG, "LCD test: vertical hardware color bars (4 s)");
    vTaskDelay(pdMS_TO_TICKS(4000));
    const esp_err_t horizontal = esp_lcd_dpi_panel_set_pattern(panel, MIPI_DSI_PATTERN_BAR_HORIZONTAL);
    if (horizontal == ESP_OK) {
        ESP_LOGI(TAG, "LCD test: horizontal hardware color bars (4 s)");
        vTaskDelay(pdMS_TO_TICKS(4000));
    }
    // Always restore framebuffer output, including when the second pattern fails.
    const esp_err_t restore = esp_lcd_dpi_panel_set_pattern(panel, MIPI_DSI_PATTERN_NONE);
    ESP_RETURN_ON_ERROR(restore, TAG, "restore framebuffer output");
    ESP_RETURN_ON_ERROR(horizontal, TAG, "horizontal test pattern");

    // Drive the real scanout framebuffer directly, before LVGL is created.
    // White -> black distinguishes stale UI pixels from retention downstream of
    // the framebuffer. Invalidate the CPU cache after writeback for readback.
    void *framebuffer = nullptr;
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_get_frame_buffer(panel, 1, &framebuffer),
                        TAG, "get diagnostic framebuffer");
    const size_t bytes = LCD_H_RES * LCD_V_RES * sizeof(uint16_t);
    for (uint8_t value : {uint8_t{0xFF}, uint8_t{0x00}}) {
        std::memset(framebuffer, value, bytes);
        ESP_RETURN_ON_ERROR(esp_cache_msync(framebuffer, bytes,
                            ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_INVALIDATE),
                            TAG, "diagnostic framebuffer writeback");
        ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(panel, 0, 0, LCD_H_RES, LCD_V_RES, framebuffer),
                            TAG, "diagnostic solid fill");
        ESP_LOGI(TAG, "LCD test: solid %s framebuffer (3 s)", value ? "white" : "black");
        if (!value) {
            const volatile uint16_t *pixels = static_cast<const uint16_t *>(framebuffer);
            size_t nonzero = 0;
            for (size_t i = 0; i < bytes / sizeof(uint16_t); ++i) nonzero += pixels[i] != 0;
            ESP_LOGI(TAG, "LCD test: black framebuffer nonzero pixels=%u", (unsigned)nonzero);
            ESP_RETURN_ON_FALSE(nonzero == 0, ESP_ERR_INVALID_STATE, TAG, "black framebuffer readback failed");
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
    ESP_LOGI(TAG, "LCD test complete; starting normal UI");
    return ESP_OK;
}
#endif

} // namespace


esp_err_t bsp_i2c_init(i2c_master_bus_handle_t *out) {
    i2c_master_bus_config_t config = {};
    config.i2c_port = I2C_NUM_0;
    config.sda_io_num = PIN_I2C_SDA;
    config.scl_io_num = PIN_I2C_SCL;
    config.clk_source = I2C_CLK_SRC_DEFAULT;
    config.glitch_ignore_cnt = 7;
    config.flags.enable_internal_pullup = true;
    return i2c_new_master_bus(&config, out);
}

esp_err_t bsp_display_start(i2c_master_bus_handle_t bus) {
    nvs_handle_t displayNvs;
    if (nvs_open("ard-display", NVS_READONLY, &displayNvs) == ESP_OK) {
        uint8_t saved = 80;
        if (nvs_get_u8(displayNvs, "brightness", &saved) == ESP_OK && saved >= 5 && saved <= 100) backlight = saved;
        nvs_close(displayNvs);
    }
    ESP_RETURN_ON_ERROR(backlightInit(), TAG, "backlight");

    // MIPI DSI PHY supply.
    esp_ldo_channel_handle_t ldo = nullptr;
    esp_ldo_channel_config_t ldoConfig = {};
    ldoConfig.chan_id = kDsiPhyLdoChannel;
    ldoConfig.voltage_mv = kDsiPhyLdoMv;
    esp_err_t err = esp_ldo_acquire_channel(&ldoConfig, &ldo);
    if (err != ESP_OK) ESP_LOGW(TAG, "DSI PHY LDO%d: %s (continuing)", kDsiPhyLdoChannel, esp_err_to_name(err));

    esp_lcd_dsi_bus_handle_t dsi;
    esp_lcd_dsi_bus_config_t busConfig = {};
    busConfig.bus_id = 0;
    busConfig.num_data_lanes = kDsiLanes;
    busConfig.phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
    busConfig.lane_bit_rate_mbps = kLaneMbps;
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&busConfig, &dsi), TAG, "dsi bus");

    esp_lcd_panel_io_handle_t dbi;
    esp_lcd_dbi_io_config_t dbiConfig = {};
    dbiConfig.virtual_channel = 0;
    dbiConfig.lcd_cmd_bits = 8;
    dbiConfig.lcd_param_bits = 8;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(dsi, &dbiConfig, &dbi), TAG, "dbi io");

    esp_lcd_dpi_panel_config_t dpi = {};
    dpi.virtual_channel = 0;
    dpi.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
    dpi.dpi_clock_freq_mhz = ard_panel_pclk_mhz;
    dpi.pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565;
    dpi.num_fbs = 1;
    dpi.video_timing = ard_panel_timing;
    dpi.flags.use_dma2d = !kSyncFullRefresh;

    jd9165_vendor_config_t vendor = {};
    vendor.init_cmds = ard_panel_init;
    vendor.init_cmds_size = ard_panel_init_count;
    vendor.mipi_config.dsi_bus = dsi;
    vendor.mipi_config.dpi_config = &dpi;
    esp_lcd_panel_dev_config_t dev = {};
    dev.reset_gpio_num = PIN_LCD_RST;
    dev.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    dev.bits_per_pixel = 16;
    dev.vendor_config = &vendor;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_jd9165(dbi, &dev, &panel), TAG, "jd9165");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "panel init");
    ESP_LOGI(TAG, "JD9165 %s panel up, %.0f MHz (RGB565, inversion off)", ard_panel_name, ard_panel_pclk_mhz);

#if CONFIG_ARD_LCD_BOOT_TEST
    ESP_RETURN_ON_ERROR(displayBootTest(), TAG, "LCD boot test");
#endif

    if ((err = touchInit(bus)) != ESP_OK) ESP_LOGE(TAG, "Touch disabled: %s", esp_err_to_name(err));

    lv_init();
    lv_tick_set_cb(tickMs);
    lvglMutex = xSemaphoreCreateRecursiveMutex();
    ESP_RETURN_ON_FALSE(lvglMutex, ESP_ERR_NO_MEM, TAG, "lvgl mutex");
    lv_display_t *display = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    const size_t bytes = LCD_H_RES * kDrawLines * sizeof(uint16_t);
    // The diagnostic full-frame path uses one 1,228,800-byte RGB565 draw buffer,
    // replacing the two 204,800-byte partial buffers. The scanout FB is separate.
    void *buf1 = heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    void *buf2 = kSyncFullRefresh ? nullptr : heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf1 || (!kSyncFullRefresh && !buf2)) {
        heap_caps_free(buf1);
        heap_caps_free(buf2);
        ESP_LOGE(TAG, "draw buffer allocation failed");
        return ESP_ERR_NO_MEM;
    }
    lv_display_set_buffers(display, buf1, buf2, bytes,
                           kSyncFullRefresh ? LV_DISPLAY_RENDER_MODE_FULL : LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    if (!kSyncFullRefresh) {
        esp_lcd_dpi_panel_event_callbacks_t callbacks = {};
        callbacks.on_color_trans_done = onColorTransDone;
        ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_register_event_callbacks(panel, &callbacks, display), TAG, "dpi cb");
    }
    ESP_LOGI(TAG, "LVGL: %s", kSyncFullRefresh ? "full redraw, synchronous CPU copy" : "partial redraw, asynchronous DMA2D copy");

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchRead);
    lv_indev_set_display(indev, display);

    // UI work and the LVGL task share core 0 with networking; motion owns core 1.
    if (xTaskCreatePinnedToCore(lvglTask, "lvgl", 8192, nullptr, 4, nullptr, 0) != pdPASS) return ESP_ERR_NO_MEM;
    bsp_backlight_set(backlight);
    return ESP_OK;
}

void bsp_backlight_set(int percent) {
    percent = percent < 5 ? 5 : percent > 100 ? 100 : percent; // never fully dark from the UI
    backlight = percent;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, kBlChannel, (1023 * percent) / 100);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, kBlChannel);
}

int bsp_backlight_get() { return backlight; }

esp_err_t bsp_backlight_save(int percent) {
    if (percent < 5 || percent > 100) return ESP_ERR_INVALID_ARG;
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("ard-display", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(nvs, "brightness", (uint8_t)percent);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}

bool bsp_lvgl_lock(uint32_t timeoutMs) {
    return lvglMutex && xSemaphoreTakeRecursive(lvglMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void bsp_lvgl_unlock() { xSemaphoreGiveRecursive(lvglMutex); }

bool bsp_touch_ready() { return touch != nullptr; }
