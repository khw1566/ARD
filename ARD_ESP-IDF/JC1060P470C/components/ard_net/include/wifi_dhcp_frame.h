#ifndef ARD_WIFI_DHCP_FRAME_H
#define ARD_WIFI_DHCP_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool arp;
    bool dhcp;
    uint8_t dhcp_type; /* DHCP option 53; zero when absent. */
    uint32_t xid;      /* Host-order BOOTP transaction identifier. */
} ard_wifi_frame_info_t;

static inline uint16_t ard_wifi_frame_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

/* Inspect Ethernet headers only. Never retain or change the supplied buffer.
 * Truncated/fragmented packets and malformed DHCP options return an empty
 * result. A valid cookie/options stream without option 53 remains DHCP with
 * dhcp_type == 0. Checksums are left to the network stack.
 */
static inline ard_wifi_frame_info_t ard_wifi_classify_frame(const void *buffer, size_t length) {
    const ard_wifi_frame_info_t empty = {0};
    if (!buffer || length < 14) return empty;
    const uint8_t *bytes = (const uint8_t *)buffer;
    size_t offset = 14;
    uint16_t ether_type = ard_wifi_frame_be16(bytes + 12);
    unsigned tags = 0;
    while (ether_type == 0x8100 || ether_type == 0x88a8) {
        if (tags++ == 2 || length - offset < 4) return empty;
        ether_type = ard_wifi_frame_be16(bytes + offset + 2);
        offset += 4;
    }
    if (ether_type == 0x0806) {
        if (length - offset < 28) return empty;
        const uint8_t *arp = bytes + offset;
        if (ard_wifi_frame_be16(arp) != 1 || ard_wifi_frame_be16(arp + 2) != 0x0800 ||
            arp[4] != 6 || arp[5] != 4) return empty;
        ard_wifi_frame_info_t result = {0};
        result.arp = true;
        return result;
    }
    if (ether_type != 0x0800 || length - offset < 20) return empty;
    const uint8_t *ip = bytes + offset;
    const size_t ihl = (size_t)(ip[0] & 15) * 4;
    const size_t ip_length = ard_wifi_frame_be16(ip + 2);
    if ((ip[0] >> 4) != 4 || ihl < 20 || ihl > length - offset ||
        ip_length < ihl + 8 || ip_length > length - offset || ip[9] != 17 ||
        (ard_wifi_frame_be16(ip + 6) & 0xbfff) != 0) return empty;
    const uint8_t *udp = ip + ihl;
    const uint16_t source = ard_wifi_frame_be16(udp);
    const uint16_t destination = ard_wifi_frame_be16(udp + 2);
    const size_t udp_length = ard_wifi_frame_be16(udp + 4);
    if (!((source == 67 && destination == 68) || (source == 68 && destination == 67)) ||
        udp_length < 8 + 240 || udp_length > ip_length - ihl) return empty;
    const uint8_t *bootp = udp + 8;
    const size_t bootp_length = udp_length - 8;
    if ((bootp[0] != 1 && bootp[0] != 2) || bootp[1] != 1 || bootp[2] != 6 ||
        bootp[236] != 99 || bootp[237] != 130 || bootp[238] != 83 || bootp[239] != 99)
        return empty;
    ard_wifi_frame_info_t result = {0};
    result.xid = ((uint32_t)bootp[4] << 24) | ((uint32_t)bootp[5] << 16) |
                 ((uint32_t)bootp[6] << 8) | bootp[7];
    bool has_type = false;
    size_t option = 240;
    while (option < bootp_length) {
        const uint8_t code = bootp[option++];
        if (code == 0) continue;
        if (code == 255) {
            result.dhcp = true;
            return result;
        }
        if (option == bootp_length) return empty;
        const size_t option_length = bootp[option++];
        if (option_length > bootp_length - option) return empty;
        if (code == 53) {
            if (has_type || option_length != 1 || bootp[option] == 0) return empty;
            result.dhcp_type = bootp[option];
            has_type = true;
        }
        option += option_length;
    }
    return empty; /* DHCP options require an end marker. */
}

#endif
