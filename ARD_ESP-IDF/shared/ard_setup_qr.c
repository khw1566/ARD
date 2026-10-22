#include "ard_setup_qr.h"
#include <stdio.h>
#include <string.h>

static ard_setup_qr_t wifi_qr, url_qr;

static char *escape(char *out, const char *text) {
    while (*text) {
        if (strchr("\\;,:\"", *text)) *out++ = '\\';
        *out++ = *text++;
    }
    *out = 0;
    return out;
}

void ard_setup_qr_init(const char *ssid, const char *password) {
    // AP SSID <=32 bytes and AP password <=63 bytes (escaped worst case).
    char text[224];
    uint8_t temp[ARD_QR_BYTES];
    strcpy(text, "WIFI:T:WPA;S:");
    char *end = escape(text + strlen(text), ssid);
    strcpy(end, ";P:");
    end = escape(end + 3, password);
    strcpy(end, ";;");
    ard_qrcodegen_encodeText(text, temp, wifi_qr.modules, ard_qrcodegen_Ecc_MEDIUM,
                         1, 6, ard_qrcodegen_Mask_AUTO, true);
    ard_qrcodegen_encodeText(ARD_SETUP_URL, temp, url_qr.modules, ard_qrcodegen_Ecc_MEDIUM,
                         1, 6, ard_qrcodegen_Mask_AUTO, true);
}

const ard_setup_qr_t *ard_setup_qr(bool wifi) { return wifi ? &wifi_qr : &url_qr; }

void ard_setup_qr_print(void) {
    for (int i = 0; i < 2; ++i) {
        const uint8_t *qr = ard_setup_qr(i == 0)->modules;
        int size = ard_qrcodegen_getSize(qr);
        printf("\n[NET] QR: %s\n", i == 0 ? "1. Join setup Wi-Fi" : "2. Open " ARD_SETUP_URL);
        if (!size) { puts("[NET] QR generation failed"); continue; }
        // ANSI black modules on white, including the four-module quiet zone.
        for (int y = -4; y < size + 4; ++y) {
            printf("\033[30;47m");
            for (int x = -4; x < size + 4; ++x)
                printf("%s", ard_qrcodegen_getModule(qr, x, y) ? "██" : "  ");
            printf("\033[0m\n");
        }
    }
}
