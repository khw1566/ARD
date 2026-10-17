// Reliable JPEG transfer over UDP (ARD image protocol v2, see ard_img_proto.h).
//
// Why not the old scheme: the Arduino sender tagged chunks only with 0/1/2
// (start/middle/end), skipped a chunk after 5 failed sends and kept going, and
// had no acknowledgement. The PC therefore concatenated whatever arrived and
// passed corrupted JPEGs on as successes. Here every chunk carries its index,
// the frame carries a CRC32, and the PC reports a received-bitmap after every
// window so lost chunks are resent (selective repeat). The window also acts as
// flow control: at most APP_TX_WINDOW_CHUNKS are in flight before the PC
// answers, so neither the ESP32 TX queue nor the PC socket buffer overflows.
#include "img_tx.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "ard_img_proto.h"
#include "ard_crc32.h"
#include "app_config.h"

static const char *TAG = "IMG_TX";

typedef enum { POLL_INCOMPLETE, POLL_COMPLETE, POLL_CRC_ERROR, POLL_REJECTED, POLL_TIMEOUT, POLL_SOCKET_ERROR } poll_result_t;

typedef struct {
    int sock;
    uint16_t frame_id;
    uint16_t count;
    uint32_t total;
    uint32_t crc;
    const uint8_t *data;
    uint8_t received[ARD_IMG_BITMAP_BYTES];
    img_tx_result_t *result;
} tx_t;

static uint16_t next_frame_id;
static uint8_t packet[sizeof(ard_img_hdr_t) + ARD_IMG_CHUNK_SIZE];
static uint8_t status_buf[sizeof(ard_img_status_t) + ARD_IMG_BITMAP_BYTES];

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }
static bool has(const tx_t *tx, uint16_t i) { return tx->received[i >> 3] & (1u << (i & 7)); }

static void fill_header(const tx_t *tx, ard_img_hdr_t *h, uint8_t type, uint16_t idx, uint16_t payload) {
    h->magic[0] = ARD_IMG_MAGIC0;
    h->magic[1] = ARD_IMG_MAGIC1;
    h->version = ARD_IMG_VERSION;
    h->type = type;
    h->frame_id = tx->frame_id;
    h->chunk_idx = idx;
    h->chunk_count = tx->count;
    h->payload_len = payload;
    h->total_len = tx->total;
    h->frame_crc32 = tx->crc;
}

// Sends one datagram. A full Wi-Fi/lwIP TX queue (ENOMEM) is back-pressure,
// not loss: wait and retry the same datagram instead of skipping it.
static bool send_datagram(tx_t *tx, const void *buf, size_t len) {
    int64_t give_up = now_ms() + APP_TX_ENOMEM_LIMIT_MS;
    while (true) {
        if (send(tx->sock, buf, len, 0) == (ssize_t)len) return true;
        if ((errno == ENOMEM || errno == ENOBUFS || errno == EAGAIN) && now_ms() < give_up) {
            vTaskDelay(pdMS_TO_TICKS(APP_TX_ENOMEM_WAIT_MS));
            continue;
        }
        snprintf(tx->result->error, sizeof(tx->result->error), "send failed: errno %d", errno);
        return false;
    }
}

static bool send_chunk(tx_t *tx, uint16_t idx) {
    uint32_t offset = (uint32_t)idx * ARD_IMG_CHUNK_SIZE;
    uint16_t len = tx->total - offset > ARD_IMG_CHUNK_SIZE ? ARD_IMG_CHUNK_SIZE : tx->total - offset;
    fill_header(tx, (ard_img_hdr_t *)packet, ARD_IMG_TYPE_DATA, idx, len);
    memcpy(packet + sizeof(ard_img_hdr_t), tx->data + offset, len);
    if (!send_datagram(tx, packet, sizeof(ard_img_hdr_t) + len)) return false;
    tx->result->packets_sent++;
    return true;
}

// Sends POLL and waits for the STATUS that echoes this sequence number.
// An unsolicited COMPLETE (sent by the PC as soon as the CRC matches) ends it early.
static poll_result_t poll_status(tx_t *tx, uint16_t seq) {
    ard_img_hdr_t poll;
    for (int attempt = 0; attempt < APP_TX_POLL_RETRIES; ++attempt) {
        fill_header(tx, &poll, ARD_IMG_TYPE_POLL, seq, 0);
        if (!send_datagram(tx, &poll, sizeof(poll))) return POLL_SOCKET_ERROR;
        tx->result->polls++;
        int64_t until = now_ms() + APP_TX_POLL_TIMEOUT_MS;
        while (now_ms() < until) {
            int n = recv(tx->sock, status_buf, sizeof(status_buf), 0);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
                snprintf(tx->result->error, sizeof(tx->result->error), "recv failed: errno %d", errno);
                return POLL_SOCKET_ERROR;
            }
            const ard_img_status_t *st = (const ard_img_status_t *)status_buf;
            if (n < (int)sizeof(*st) || st->magic[0] != ARD_IMG_MAGIC0 || st->magic[1] != ARD_IMG_MAGIC1 ||
                st->version != ARD_IMG_VERSION || st->type != ARD_IMG_TYPE_STATUS || st->frame_id != tx->frame_id)
                continue; // late STATUS of an earlier frame or foreign datagram
            if (st->state == ARD_IMG_STATE_COMPLETE) return POLL_COMPLETE;
            if (st->state == ARD_IMG_STATE_CRC_ERROR) return POLL_CRC_ERROR;
            if (st->state == ARD_IMG_STATE_REJECTED) return POLL_REJECTED;
            if (st->echo_seq != seq) continue; // stale answer to an earlier POLL
            size_t bitmap = (tx->count + 7) / 8;
            if ((size_t)n < sizeof(*st) + bitmap || st->chunk_count != tx->count) continue;
            memcpy(tx->received, status_buf + sizeof(*st), bitmap);
            return POLL_INCOMPLETE;
        }
        ESP_LOGD(TAG, "frame %u: no STATUS for poll %u (attempt %d)", tx->frame_id, seq, attempt + 1);
    }
    return POLL_TIMEOUT;
}

static int open_socket(const char *host, uint16_t port, img_tx_result_t *result) {
    struct sockaddr_in dest = {.sin_family = AF_INET, .sin_port = htons(port)};
    if (inet_pton(AF_INET, host, &dest.sin_addr) != 1) {
        snprintf(result->error, sizeof(result->error), "PC address is not IPv4: %s", host);
        return -1;
    }
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        snprintf(result->error, sizeof(result->error), "socket failed: errno %d", errno);
        return -1;
    }
    // Short receive timeout so the POLL loop can check its own deadline.
    struct timeval tv = {.tv_sec = 0, .tv_usec = 10000};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    // connect() filters STATUS datagrams to the PC's address and port.
    if (connect(sock, (struct sockaddr *)&dest, sizeof(dest)) != 0) {
        snprintf(result->error, sizeof(result->error), "connect failed: errno %d", errno);
        close(sock);
        return -1;
    }
    return sock;
}

bool img_tx_send(const char *host, uint16_t port, const uint8_t *data, size_t len, img_tx_result_t *result) {
    memset(result, 0, sizeof(*result));
    if (!len || len > ARD_IMG_MAX_FRAME) {
        snprintf(result->error, sizeof(result->error), "frame size %u outside 1..%u", (unsigned)len, ARD_IMG_MAX_FRAME);
        return false;
    }
    if (!next_frame_id) next_frame_id = (uint16_t)(esp_random() | 1);
    tx_t tx = {
        .frame_id = next_frame_id++,
        .count = (len + ARD_IMG_CHUNK_SIZE - 1) / ARD_IMG_CHUNK_SIZE,
        .total = len,
        .crc = ard_crc32(0, data, len),
        .data = data,
        .result = result,
    };
    result->frame_id = tx.frame_id;
    result->chunks = tx.count;
    tx.sock = open_socket(host, port, result);
    if (tx.sock < 0) return false;

    int64_t start = now_ms();
    int64_t deadline = start + APP_TX_BASE_DEADLINE_MS + len / 100; // +1 s per 100 KB
    uint16_t next_new = 0, seq = 0;
    int crc_restarts = 1;
    ESP_LOGI(TAG, "frame %u: %u bytes, %u chunks, crc32 %08lx -> %s:%u", tx.frame_id, (unsigned)len,
             tx.count, (unsigned long)tx.crc, host, port);
    while (true) {
        // 1) resend chunks the last STATUS reported missing, 2) continue with new ones.
        uint16_t burst = 0;
        for (uint16_t i = 0; i < next_new && burst < APP_TX_WINDOW_CHUNKS; ++i) {
            if (has(&tx, i)) continue;
            if (!send_chunk(&tx, i)) goto done;
            result->retransmits++;
            burst++;
        }
        while (next_new < tx.count && burst < APP_TX_WINDOW_CHUNKS) {
            if (!send_chunk(&tx, next_new++)) goto done;
            burst++;
        }
        switch (poll_status(&tx, ++seq)) {
        case POLL_COMPLETE:
            result->ok = true;
            goto done;
        case POLL_INCOMPLETE:
            break;
        case POLL_CRC_ERROR:
            if (crc_restarts-- > 0) {
                ESP_LOGW(TAG, "frame %u: PC reported CRC mismatch, resending whole frame", tx.frame_id);
                memset(tx.received, 0, sizeof(tx.received));
                next_new = 0;
                break;
            }
            snprintf(result->error, sizeof(result->error), "CRC mismatch after full resend");
            goto done;
        case POLL_REJECTED:
            snprintf(result->error, sizeof(result->error), "PC rejected frame header");
            goto done;
        case POLL_TIMEOUT:
            snprintf(result->error, sizeof(result->error),
                     "no STATUS from PC (is ARD_MAIN running, UDP %u allowed in firewall?)", port);
            goto done;
        case POLL_SOCKET_ERROR:
            goto done;
        }
        if (now_ms() > deadline) {
            snprintf(result->error, sizeof(result->error), "deadline exceeded after %u polls", (unsigned)result->polls);
            goto done;
        }
    }
done:
    close(tx.sock);
    result->elapsed_ms = now_ms() - start;
    if (result->ok)
        ESP_LOGI(TAG, "frame %u delivered: %lu ms, %lu packets (%lu resent), %lu polls", tx.frame_id,
                 (unsigned long)result->elapsed_ms, (unsigned long)result->packets_sent,
                 (unsigned long)result->retransmits, (unsigned long)result->polls);
    else
        ESP_LOGE(TAG, "frame %u FAILED: %s", tx.frame_id, result->error);
    return result->ok;
}
