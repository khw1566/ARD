#pragma once
#include <stddef.h>

// A truncated command must never be interpreted as a valid motor command.
class BoundedLine {
public:
    enum Result { Pending, Ready, TooLong };
    Result push(char c) {
        if (c == '\n' || c == '\r') {
            if (discarding) { reset(); return TooLong; }
            if (!length) return Pending;
            text[length] = '\0';
            return Ready;
        }
        if (discarding) return Pending;
        if (c == 8 || c == 127) { if (length) --length; return Pending; }
        if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) > 126) { discarding = true; return Pending; }
        if (length == sizeof(text) - 1) { discarding = true; return Pending; }
        text[length++] = c;
        return Pending;
    }
    const char* value() const { return text; }
    bool partial() const { return length || discarding; }
    void reset() { length = 0; discarding = false; }
private:
    char text[128] = {};
    size_t length = 0;
    bool discarding = false;
};
