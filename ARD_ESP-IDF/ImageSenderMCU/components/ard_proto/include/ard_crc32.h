#pragma once
#include <stddef.h>
#include <stdint.h>

// Standard CRC-32 (IEEE 802.3, reflected 0xEDB88320), identical to Python's
// zlib.crc32. Pass 0 as crc for the first block and the result for the next.
uint32_t ard_crc32(uint32_t crc, const void *data, size_t len);
