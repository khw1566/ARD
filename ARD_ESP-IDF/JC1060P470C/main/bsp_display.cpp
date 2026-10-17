#include "bsp_display.h"
#include <atomic>
#include <initializer_list>
#include "board.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
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
constexpr int kDrawLines = 100;          // LVGL partial buffer height
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
    // DMA2D copies into the frame buffer; onColorTransDone releases the draw buffer.
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, pixels);
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
    dpi.flags.use_dma2d = true;

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
    ESP_LOGI(TAG, "JD9165 %s panel up, %.0f MHz", ard_panel_name, ard_panel_pclk_mhz);

    if ((err = touchInit(bus)) != ESP_OK) ESP_LOGE(TAG, "Touch disabled: %s", esp_err_to_name(err));

    lv_init();
    lv_tick_set_cb(tickMs);
    lvglMutex = xSemaphoreCreateRecursiveMutex();
    ESP_RETURN_ON_FALSE(lvglMutex, ESP_ERR_NO_MEM, TAG, "lvgl mutex");
    lv_display_t *display = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    const size_t bytes = LCD_H_RES * kDrawLines * sizeof(uint16_t);
    // PSRAM draw buffers; the DPI driver writes the cache back before DMA2D reads them.
    void *buf1 = heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM);
    void *buf2 = heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(buf1 && buf2, ESP_ERR_NO_MEM, TAG, "draw buffers");
    lv_display_set_buffers(display, buf1, buf2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    esp_lcd_dpi_panel_event_callbacks_t callbacks = {};
    callbacks.on_color_trans_done = onColorTransDone;
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_register_event_callbacks(panel, &callbacks, display), TAG, "dpi cb");

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

bool bsp_lvgl_lock(uint32_t timeoutMs) {
    return lvglMutex && xSemaphoreTakeRecursive(lvglMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void bsp_lvgl_unlock() { xSemaphoreGiveRecursive(lvglMutex); }

bool bsp_touch_ready() { return touch != nullptr; }
