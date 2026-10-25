#include "hmi_setup.h"
#include "ard_network.h"
#include "ard_setup_qr.h"

static void scene(void *ctx) {
    bool wifi = *(bool *)ctx;
    const uint8_t *qr = ard_setup_qr(wifi)->modules;
    int size = ard_qrcodegen_getSize(qr);
    gfx_fill_rect(0, 0, 240, 320, 0xffff);
    gfx_text(120, 8, wifi ? "1. Join setup Wi-Fi" : "2. Open setup page", 0, 1, GFX_TC);
    int scale = 200 / (size + 8), left = (240 - size * scale) / 2;
    int top = 28 + 4 * scale; // Four white modules on every side of the symbol.
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            if (ard_qrcodegen_getModule(qr, x, y)) gfx_fill_rect(left + x * scale, top + y * scale, scale, scale, 0);
    if (wifi) {
        gfx_text(120, 232, ard_network_hostname(), 0, 1, GFX_TC);
        gfx_text(120, 248, "Open Wi-Fi - no password", 0, 1, GFX_TC);
    } else {
        gfx_text(120, 232, "192.168.4.1/network", 0, 1, GFX_TC);
        gfx_text(120, 248, "No login required", 0, 1, GFX_TC);
    }
    gfx_text(120, 266, "Hold header to reopen QR", 0, 1, GFX_TC);
    gfx_fill_rect(4, 282, 112, 34, 0x176b);
    gfx_fill_rect(124, 282, 112, 34, 0x176b);
    gfx_text(60, 294, "Next QR", 0xffff, 1, GFX_TC);
    gfx_text(180, 294, "Done", 0xffff, 1, GFX_TC);
}

void hmi_setup_show(const gfx_target_t *target, bool wifi) {
    gfx_render(target, 0, target->height, scene, &wifi);
}
