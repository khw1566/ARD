#pragma once
// ARD image transfer protocol v2 (ESP32 -> PC, UDP 5001).
// Must match ARD_MAIN/backend/network/image_protocol.py byte for byte.
//
// Every multi-byte field is little-endian. A JPEG frame is split into fixed
// ARD_IMG_CHUNK_SIZE chunks; chunk i lives at offset i * ARD_IMG_CHUNK_SIZE.
//
//  ESP32                                   PC
//   DATA(frame, idx, payload) x window -->
//   POLL(frame, seq)                   -->
//                                      <-- STATUS(frame, seq, state, bitmap)
//   DATA(missing idx ...)              -->   (selective repeat)
//   ...                                <-- STATUS(COMPLETE) when CRC32 matches
#include <stdint.h>

#define ARD_IMG_MAGIC0 'A'
#define ARD_IMG_MAGIC1 'I'
#define ARD_IMG_VERSION 2

#define ARD_IMG_TYPE_DATA 1
#define ARD_IMG_TYPE_POLL 2
#define ARD_IMG_TYPE_STATUS 3

// 20 B header + 1400 B payload + 28 B IP/UDP = 1448 B < 1500 B Wi-Fi MTU.
#define ARD_IMG_CHUNK_SIZE 1400
#define ARD_IMG_MAX_CHUNKS 2048
#define ARD_IMG_MAX_FRAME (ARD_IMG_CHUNK_SIZE * ARD_IMG_MAX_CHUNKS)
#define ARD_IMG_BITMAP_BYTES (ARD_IMG_MAX_CHUNKS / 8)

// STATUS.state
#define ARD_IMG_STATE_INCOMPLETE 0
#define ARD_IMG_STATE_COMPLETE 1  // all chunks received and frame CRC32 matched
#define ARD_IMG_STATE_CRC_ERROR 2 // all chunks received but CRC32 mismatched; resend all
#define ARD_IMG_STATE_REJECTED 3  // header invalid for the PC (size/limits)

// STATUS.echo_seq value for an unsolicited STATUS sent when a frame completes.
#define ARD_IMG_SEQ_UNSOLICITED 0xFFFF

// DATA and POLL share this header. POLL carries no payload and puts its
// poll sequence number in chunk_idx; payload_len is 0.
typedef struct __attribute__((packed)) {
    uint8_t magic[2];
    uint8_t version;
    uint8_t type;
    uint16_t frame_id;
    uint16_t chunk_idx;
    uint16_t chunk_count;
    uint16_t payload_len;
    uint32_t total_len;
    uint32_t frame_crc32;
} ard_img_hdr_t;

// PC -> ESP32. Followed by ceil(chunk_count / 8) bitmap bytes;
// bit (i % 8) of byte (i / 8) set means chunk i was received.
typedef struct __attribute__((packed)) {
    uint8_t magic[2];
    uint8_t version;
    uint8_t type;
    uint16_t frame_id;
    uint16_t chunk_count;
    uint16_t received_count;
    uint8_t state;
    uint8_t reserved;
    uint16_t echo_seq;
} ard_img_status_t;

_Static_assert(sizeof(ard_img_hdr_t) == 20, "DATA header must be 20 bytes");
_Static_assert(sizeof(ard_img_status_t) == 14, "STATUS header must be 14 bytes");
