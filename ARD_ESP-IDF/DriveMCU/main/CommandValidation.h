#pragma once
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <initializer_list>

inline bool decimalInteger(const char *text, const char *end) {
    if (text < end && (*text == '+' || *text == '-')) ++text;
    if (text == end) return false;
    for (; text < end; ++text) if (*text < '0' || *text > '9') return false;
    return true;
}

inline bool commandNumber(const char *text, double minimum, double maximum, bool integer = false) {
    if (!*text) return false;
    char *end = nullptr;
    const double value = std::strtod(text, &end);
    return end != text && *end == '\0' && std::isfinite(value) &&
        value >= minimum && value <= maximum && (!integer || decimalInteger(text, end));
}

inline bool validDriveCommand(const char *command) {
    for (const char *simple : {"x", "stop", "e", "enable", "d", "disable", "home", "zero", "w", "h", "?", "mza"})
        if (!std::strcmp(command, simple)) return true;
    if (command[0] == 'b' && command[1] != 'v') {
        char *end = nullptr;
        const double first = std::strtod(command + 1, &end);
        if (end == command + 1 || *end != ',' || !std::isfinite(first) ||
            first < -2147483647.0 || first > 2147483647.0 || !decimalInteger(command + 1, end)) return false;
        return commandNumber(end + 1, -2147483647.0, 2147483647.0, true);
    }
    if (command[0] == 'j') { // j<x>,<y>,<z>, each -1000..1000
        const char *part = command + 1;
        for (int i = 0; i < 3; ++i) {
            const char *end = i < 2 ? std::strchr(part, ',') : part + std::strlen(part);
            if (!end || !decimalInteger(part, end)) return false;
            const long value = std::strtol(part, nullptr, 10);
            if (value < -1000 || value > 1000) return false;
            part = end + 1;
        }
        return true;
    }
    for (const char *prefix : {"df", "db", "dr", "dl"})
        if (!std::strncmp(command, prefix, 2))
            return !command[2] || commandNumber(command + 2, -2147483647.0, 2147483647.0, true);
    for (const char *prefix : {"mxr", "myr", "mzr"})
        if (!std::strncmp(command, prefix, 3)) return commandNumber(command + 3, -2147483647.0, 2147483647.0, true);
    for (const char *prefix : {"mxs", "mys", "mxa", "mya", "mza"})
        if (!std::strncmp(command, prefix, 3)) return commandNumber(command + 3, 1, 100000, true);
    for (const char *simple : {"mxx", "myx", "mzx", "mxe", "mye", "mxd", "myd"})
        if (!std::strcmp(command, simple)) return true;
    if (!std::strncmp(command, "mzs", 3)) return commandNumber(command + 3, .000001, 100000);
    for (const char *prefix : {"mzp", "mzi", "mzd"})
        if (!std::strncmp(command, prefix, 3)) return commandNumber(command + 3, 0, 10000);
    if (!std::strncmp(command, "mzt", 3)) return commandNumber(command + 3, 1, 2000, true);
    if (!std::strncmp(command, "mzl", 3)) { // mzl0 or mzl<min>,<max> (ToF mm, max - min >= 10)
        if (!std::strcmp(command + 3, "0")) return true;
        const char *comma = std::strchr(command + 3, ',');
        if (!comma || !decimalInteger(command + 3, comma) || !commandNumber(comma + 1, 1, 2000, true)) return false;
        const long low = std::strtol(command + 3, nullptr, 10), high = std::strtol(comma + 1, nullptr, 10);
        return low >= 1 && high - low >= 10;
    }
    if (!std::strncmp(command, "mzv", 3))
        return commandNumber(command + 3, -1, 1, true) && std::strtol(command + 3, nullptr, 10) != 0;
    if (!std::strncmp(command, "mzc", 3)) return commandNumber(command + 3, 10000, 400000, true);
    if (!std::strncmp(command, "mzh", 3)) return commandNumber(command + 3, 250, 60000, true);
    if (!std::strncmp(command, "mzm", 3) || !std::strncmp(command, "gps", 3)) return commandNumber(command + 3, 0, 1, true);
    if (!std::strncmp(command, "md", 2)) return commandNumber(command + 2, 0, 60000, true);
    if (!std::strncmp(command, "bv", 2)) return commandNumber(command + 2, 0, 255, true);
    if (command[0] == 'r') return commandNumber(command + 1, -2147483647.0, 2147483647.0, true);
    if (command[0] == 's' || command[0] == 'a') return commandNumber(command + 1, 1, 100000, true);
    return false;
}
