#include "gfx.h"
#include <string.h>
#include "font8x8.h"
#include "korean_font.h"

static uint16_t *band;
static int band_y, band_w, band_h;

void gfx_render(const gfx_target_t *t, int y0, int y1, gfx_scene_fn scene, void *ctx) {
    if (y0 < 0) y0 = 0;
    if (y1 > t->height) y1 = t->height;
    for (int y = y0; y < y1; y += t->band_h) {
        band = t->acquire(t->user);
        band_y = y;
        band_w = t->width;
        band_h = (y1 - y < t->band_h) ? y1 - y : t->band_h;
        scene(ctx);
        t->flush(t->user, 0, band_y, band_w, band_h, band);
    }
    band = NULL;
}

// Clip a span to the current band; returns false when nothing remains.
static inline bool clip(int *x, int *y, int *w, int *h) {
    if (!band) return false;
    int x1 = *x + *w, y1 = *y + *h;
    if (*x < 0) *x = 0;
    if (*y < band_y) *y = band_y;
    if (x1 > band_w) x1 = band_w;
    if (y1 > band_y + band_h) y1 = band_y + band_h;
    *w = x1 - *x;
    *h = y1 - *y;
    return *w > 0 && *h > 0;
}

void gfx_fill_rect(int x, int y, int w, int h, uint16_t color) {
    if (!clip(&x, &y, &w, &h)) return;
    const uint16_t c = GFX_BE(color);
    for (int row = 0; row < h; ++row) {
        uint16_t *p = band + (y - band_y + row) * band_w + x;
        for (int i = 0; i < w; ++i) p[i] = c;
    }
}

void gfx_hline(int x, int y, int w, uint16_t color) { gfx_fill_rect(x, y, w, 1, color); }
void gfx_vline(int x, int y, int h, uint16_t color) { gfx_fill_rect(x, y, 1, h, color); }

void gfx_rect(int x, int y, int w, int h, uint16_t color) {
    gfx_hline(x, y, w, color);
    gfx_hline(x, y + h - 1, w, color);
    gfx_vline(x, y, h, color);
    gfx_vline(x + w - 1, y, h, color);
}

static int isqrt(int v) {
    int r = 0;
    while ((r + 1) * (r + 1) <= v) ++r;
    return r;
}

// Horizontal inset of row i of a w x h box with corner radius r.
static int corner_inset(int i, int h, int r) {
    int dy;
    if (i < r) dy = r - i;
    else if (i >= h - r) dy = i - (h - 1 - r);
    else return 0;
    return r - isqrt(r * r - dy * dy);
}

void gfx_fill_round_rect(int x, int y, int w, int h, int r, uint16_t color) {
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    for (int i = 0; i < h; ++i) {
        int yy = y + i;
        if (yy < band_y || yy >= band_y + band_h) continue;
        int a = corner_inset(i, h, r);
        gfx_hline(x + a, yy, w - 2 * a, color);
    }
}

void gfx_round_rect(int x, int y, int w, int h, int r, uint16_t color) {
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    for (int i = 0; i < h; ++i) {
        int yy = y + i;
        if (yy < band_y || yy >= band_y + band_h) continue;
        int a = corner_inset(i, h, r);
        if (i == 0 || i == h - 1) {
            gfx_hline(x + a, yy, w - 2 * a, color);
            continue;
        }
        // Span reaches the inset of the neighbouring row nearer the edge, so steep arcs stay closed.
        int edge = i < h / 2 ? corner_inset(i - 1, h, r) : corner_inset(i + 1, h, r);
        int len = edge - a > 1 ? edge - a : 1;
        gfx_hline(x + a, yy, len, color);
        gfx_hline(x + w - a - len, yy, len, color);
    }
}

void gfx_fill_circle(int cx, int cy, int r, uint16_t color) {
    for (int dy = -r; dy <= r; ++dy) {
        int yy = cy + dy;
        if (yy < band_y || yy >= band_y + band_h) continue;
        int half = isqrt(r * r - dy * dy);
        gfx_hline(cx - half, yy, half * 2 + 1, color);
    }
}

void gfx_image(int x, int y, const gfx_image_t *img) {
    if (!img || !img->px) return;
    int cx = x, cy = y, w = img->w, h = img->h;
    if (!clip(&cx, &cy, &w, &h)) return;
    for (int row = 0; row < h; ++row) {
        const uint16_t *src = img->px + (cy - y + row) * img->w + (cx - x);
        memcpy(band + (cy - band_y + row) * band_w + cx, src, w * sizeof(uint16_t));
    }
}

// ---- 8x8 ASCII ------------------------------------------------------------
// Multi-line labels get 2 px between lines; touching 8x8 rows were hard to read.
#define LINE_PITCH(scale) (8 * (scale) + 2)

static int line_len(const char *s) {
    int n = 0;
    while (s[n] && s[n] != '\n') ++n;
    return n;
}

int gfx_text_width(const char *s, int scale) {
    int widest = 0;
    while (*s) {
        int n = line_len(s);
        if (n > widest) widest = n;
        s += n;
        if (*s == '\n') ++s;
    }
    return widest * 8 * scale;
}

static void glyph8(int x, int y, unsigned char c, uint16_t color, int scale) {
    if (y + 8 * scale <= band_y || y >= band_y + band_h) return;
    const unsigned char *bmp = font8x8_basic[c & 0x7F];
    for (int row = 0; row < 8; ++row) {
        unsigned char bits = bmp[row];
        for (int col = 0; bits; ++col, bits >>= 1)
            if (bits & 1) gfx_fill_rect(x + col * scale, y + row * scale, scale, scale, color);
    }
}

void gfx_text(int x, int y, const char *s, uint16_t color, int scale, gfx_align_t align) {
    if (!s) return;
    int lines = 1;
    for (const char *p = s; *p; ++p) lines += *p == '\n';
    int cy = y;
    if (align >= GFX_MC) cy -= (lines * LINE_PITCH(scale) - 2) / 2;
    while (true) {
        int n = line_len(s), tw = n * 8 * scale, dx = x;
        if (align == GFX_TC || align == GFX_MC) dx -= tw / 2;
        if (align == GFX_TR || align == GFX_MR) dx -= tw;
        for (int i = 0; i < n; ++i) glyph8(dx + i * 8 * scale, cy, (unsigned char)s[i], color, scale);
        s += n;
        if (*s != '\n') break;
        ++s;
        cy += LINE_PITCH(scale);
    }
}

// ---- UTF-8 / Hangul 16x16 ---------------------------------------------------

static uint32_t utf8_next(const char **ps) {
    const unsigned char *s = (const unsigned char *)*ps;
    uint32_t c = *s++;
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    if (extra) c &= 0x3F >> extra;
    for (; extra && (*s & 0xC0) == 0x80; --extra) c = (c << 6) | (*s++ & 0x3F);
    if (extra) c = '?'; // truncated sequence
    *ps = (const char *)s;
    return c;
}

static const uint8_t *hangul_glyph(uint32_t code) {
    int lo = 0, hi = (int)hangul_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (hangul_index[mid].code == code) return hangul_bitmaps + hangul_index[mid].offset;
        if (hangul_index[mid].code < code) lo = mid + 1;
        else hi = mid - 1;
    }
    return NULL;
}

static int advance16(uint32_t c) { return c < 0x80 ? 8 : 16; }

static bool is_ascii(const char *s) {
    for (; *s; ++s)
        if ((unsigned char)*s >= 0x80) return false;
    return true;
}

int gfx_text_utf8_width(const char *s) {
    if (is_ascii(s)) return gfx_text_width(s, 1);
    int w = 0;
    while (*s) w += advance16(utf8_next(&s));
    return w;
}

int gfx_text_utf8_height(const char *s) { return is_ascii(s) ? 8 : 16; }

int gfx_utf8_fit(const char *s, int max_px, char *out, int out_size) {
    const char *p = s;
    int w = 0;
    while (*p) {
        const char *q = p;
        int adv = advance16(utf8_next(&q));
        if (w + adv > max_px || q - s >= out_size) break;
        w += adv;
        p = q;
    }
    memcpy(out, s, p - s);
    out[p - s] = 0;
    return (int)(p - s);
}

static void glyph16(int x, int y, const uint8_t *bmp, int first_col, int cols, uint16_t color) {
    if (y + 16 <= band_y || y >= band_y + band_h) return;
    for (int row = 0; row < 16; ++row) {
        uint16_t bits = (uint16_t)(bmp[row * 2] << 8 | bmp[row * 2 + 1]);
        for (int col = 0; col < cols; ++col)
            if (bits & (0x8000 >> (first_col + col))) gfx_fill_rect(x + col, y + row, 1, 1, color);
    }
}

void gfx_text_utf8(int x, int y, const char *s, uint16_t color, gfx_align_t align) {
    if (!s) return;
    if (is_ascii(s)) {
        gfx_text(x, y, s, color, 1, align);
        return;
    }
    int tw = gfx_text_utf8_width(s);
    if (align == GFX_TC || align == GFX_MC) x -= tw / 2;
    if (align == GFX_TR || align == GFX_MR) x -= tw;
    if (align >= GFX_MC) y -= 8;
    while (*s) {
        uint32_t c = utf8_next(&s);
        const uint8_t *bmp = c > ' ' ? hangul_glyph(c) : NULL;
        if (c < 0x80) {
            // ASCII glyphs sit in columns 4..11 of the 16x16 cell.
            if (bmp) glyph16(x, y, bmp, 4, 8, color);
            x += 8;
        } else {
            if (bmp) glyph16(x, y, bmp, 0, 16, color);
            else gfx_rect(x + 2, y + 2, 12, 12, color); // glyph missing from the subset
            x += 16;
        }
    }
}
