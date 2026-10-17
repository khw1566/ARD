#include "jpeg.h"
#include <stdlib.h>
#include <string.h>
#include "rom/tjpgd.h"

#define WORK_SIZE 3100 // TJpgDec R0.01 (ROM) needs 3100 bytes for baseline images

typedef struct {
    const uint8_t *data;
    size_t len, pos;
    gfx_image_t *img;
} jpeg_ctx_t;

static UINT input(JDEC *jd, BYTE *buf, UINT n) {
    jpeg_ctx_t *c = jd->device;
    if (n > c->len - c->pos) n = c->len - c->pos;
    if (buf) memcpy(buf, c->data + c->pos, n);
    c->pos += n;
    return n;
}

static UINT output(JDEC *jd, void *bitmap, JRECT *r) {
    jpeg_ctx_t *c = jd->device;
    const uint8_t *rgb = bitmap; // JD_FORMAT 0: RGB888
    const int w = r->right - r->left + 1;
    for (int y = r->top; y <= r->bottom; ++y) {
        for (int i = 0; i < w; ++i, rgb += 3) {
            int x = r->left + i;
            if (x >= c->img->w || y >= c->img->h) continue;
            uint16_t px = (uint16_t)(((rgb[0] & 0xF8) << 8) | ((rgb[1] & 0xFC) << 3) | (rgb[2] >> 3));
            c->img->px[y * c->img->w + x] = GFX_BE(px);
        }
    }
    return 1;
}

esp_err_t jpeg_decode(const uint8_t *data, size_t len, int max_w, int max_h, gfx_image_t *out) {
    memset(out, 0, sizeof(*out));
    jpeg_ctx_t ctx = {.data = data, .len = len, .img = out};
    JDEC jd;
    void *work = malloc(WORK_SIZE);
    if (!work) return ESP_ERR_NO_MEM;
    esp_err_t err = ESP_FAIL;
    if (jd_prepare(&jd, input, work, WORK_SIZE, &ctx) == JDR_OK) {
        int scale = 0;
        while (scale < 3 && ((int)(jd.width >> scale) > max_w || (int)(jd.height >> scale) > max_h)) ++scale;
        out->w = (jd.width + (1 << scale) - 1) >> scale;
        out->h = (jd.height + (1 << scale) - 1) >> scale;
        if (out->w > max_w || out->h > max_h) {
            err = ESP_ERR_INVALID_SIZE;
        } else if (!(out->px = calloc((size_t)out->w * out->h, sizeof(uint16_t)))) {
            err = ESP_ERR_NO_MEM;
        } else if (jd_decomp(&jd, output, scale) == JDR_OK) {
            err = ESP_OK;
        }
    }
    free(work);
    if (err != ESP_OK) {
        free(out->px);
        memset(out, 0, sizeof(*out));
    }
    return err;
}
