#pragma once
// Band renderer: the scene callback draws the whole screen with absolute
// coordinates, and every primitive is clipped to the band currently being
// filled. Each band is sent to the panel once, so redraws never flicker.
// Portable C (no ESP-IDF headers) so the PC simulator can build the same UI.
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Pixels are RGB565 stored big-endian, the order the ILI9341 expects on the wire.
#define GFX_BE(c) ((uint16_t)((((c) & 0xFF) << 8) | (((c) >> 8) & 0xFF)))

typedef struct {
    uint16_t w, h;
    uint16_t *px; // big-endian RGB565, w * h
} gfx_image_t;

typedef enum {
    GFX_TL = 0, // left, top
    GFX_TC = 1, // centre, top
    GFX_TR = 2, // right, top
    GFX_MC = 3, // centre, middle
    GFX_MR = 4, // right, middle
    GFX_ML = 5, // left, middle
} gfx_align_t;

typedef struct {
    int width, height, band_h;
    uint16_t *(*acquire)(void *user);
    void (*flush)(void *user, int x, int y, int w, int h, uint16_t *px);
    void *user;
} gfx_target_t;

typedef void (*gfx_scene_fn)(void *ctx);

// Redraws rows [y0, y1) by running the scene once per band.
void gfx_render(const gfx_target_t *target, int y0, int y1, gfx_scene_fn scene, void *ctx);

void gfx_fill_rect(int x, int y, int w, int h, uint16_t color);
void gfx_hline(int x, int y, int w, uint16_t color);
void gfx_vline(int x, int y, int h, uint16_t color);
void gfx_rect(int x, int y, int w, int h, uint16_t color);
void gfx_fill_round_rect(int x, int y, int w, int h, int r, uint16_t color);
void gfx_round_rect(int x, int y, int w, int h, int r, uint16_t color);
void gfx_fill_circle(int cx, int cy, int r, uint16_t color);
void gfx_image(int x, int y, const gfx_image_t *img);

// 8x8 ASCII font scaled by `scale`; '\n' starts a new line.
void gfx_text(int x, int y, const char *s, uint16_t color, int scale, gfx_align_t align);
int gfx_text_width(const char *s, int scale);
// UTF-8 with the 16x16 Hangul font. ASCII uses 8 px advance, Hangul 16 px.
// Falls back to gfx_text when the string is pure ASCII so mixed screens stay uniform.
void gfx_text_utf8(int x, int y, const char *s, uint16_t color, gfx_align_t align);
int gfx_text_utf8_width(const char *s);
int gfx_text_utf8_height(const char *s);
// Copies at most max_px pixels of UTF-8 text into out without splitting a character.
// Returns the number of bytes consumed from s.
int gfx_utf8_fit(const char *s, int max_px, char *out, int out_size);

#ifdef __cplusplus
}
#endif
