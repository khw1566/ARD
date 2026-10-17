#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool ok;
    uint16_t frame_id;
    uint16_t chunks;
    uint32_t packets_sent;  // DATA packets including retransmissions
    uint32_t retransmits;   // DATA packets resent after a STATUS reported them missing
    uint32_t polls;
    uint32_t elapsed_ms;
    char error[96];
} img_tx_result_t;

// Sends one JPEG with the ARD image protocol v2 and returns only after the PC
// confirmed every chunk and the frame CRC32, or after a hard failure.
// Not reentrant: call from the capture task only.
bool img_tx_send(const char *host, uint16_t port, const uint8_t *data, size_t len, img_tx_result_t *result);
