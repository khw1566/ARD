#include "ard_crc32.h"
#include <stdbool.h>

static uint32_t table[256];
static bool ready;

static void build_table(void) {
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        table[i] = c;
    }
    ready = true;
}

uint32_t ard_crc32(uint32_t crc, const void *data, size_t len) {
    if (!ready) build_table();
    const uint8_t *p = data;
    crc = ~crc;
    while (len--) crc = table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}
