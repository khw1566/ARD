#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static inline int ard_form_hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
// Decode exactly one bounded form value. Reject NULs, bad escapes and duplicates.
static inline bool ard_form_field(const char *body, const char *name, char *out, size_t capacity) {
    bool found = false;
    for (const char *p = body; *p;) {
        const char *end = strchr(p, '&');
        if (!end) end = p + strlen(p);
        size_t keylen = strlen(name);
        if ((size_t)(end - p) > keylen && !strncmp(p, name, keylen) && p[keylen] == '=') {
            if (found) return false;
            found = true;
            size_t count = 0;
            for (const char *v = p + keylen + 1; v < end; ++v) {
                unsigned char c = *v;
                if (c == '%') {
                    if (end - v < 3 || ard_form_hex(v[1]) < 0 || ard_form_hex(v[2]) < 0) return false;
                    c = (ard_form_hex(v[1]) << 4) | ard_form_hex(v[2]); v += 2;
                } else if (c == '+') c = ' ';
                if (!c || count + 1 >= capacity) return false;
                out[count++] = c;
            }
            out[count] = 0;
        }
        p = *end ? end + 1 : end;
    }
    return found;
}
