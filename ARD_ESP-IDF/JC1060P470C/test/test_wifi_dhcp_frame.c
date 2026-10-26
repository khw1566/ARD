#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "wifi_dhcp_frame.h"

static uint8_t frame[400];
static size_t ip_offset, udp_offset, bootp_offset, frame_length;
static unsigned checks;

static void put16(size_t offset, uint16_t value) {
    frame[offset] = (uint8_t)(value >> 8);
    frame[offset + 1] = (uint8_t)value;
}

static void resize_payload(size_t bootp_length) {
    put16(udp_offset + 4, (uint16_t)(8 + bootp_length));
    put16(ip_offset + 2, (uint16_t)(udp_offset - ip_offset + 8 + bootp_length));
    frame_length = bootp_offset + bootp_length;
}

static void packet(unsigned tags, unsigned ihl_words, uint8_t message) {
    memset(frame, 0, sizeof(frame));
    size_t next = 12;
    for (unsigned i = 0; i < tags; ++i) {
        put16(next, i == 0 && tags == 2 ? 0x88a8 : 0x8100);
        put16(next + 2, 7);
        next += 4;
    }
    put16(next, 0x0800);
    ip_offset = next + 2;
    udp_offset = ip_offset + ihl_words * 4;
    bootp_offset = udp_offset + 8;
    frame[ip_offset] = (uint8_t)(0x40 | ihl_words);
    frame[ip_offset + 9] = 17;
    const bool server = message == 2 || message == 5 || message == 6;
    put16(udp_offset, server ? 67 : 68);
    put16(udp_offset + 2, server ? 68 : 67);
    frame[bootp_offset] = server ? 2 : 1;
    frame[bootp_offset + 1] = 1;
    frame[bootp_offset + 2] = 6;
    frame[bootp_offset + 4] = 0x12;
    frame[bootp_offset + 5] = 0x34;
    frame[bootp_offset + 6] = 0xab;
    frame[bootp_offset + 7] = 0xcd;
    frame[bootp_offset + 236] = 99;
    frame[bootp_offset + 237] = 130;
    frame[bootp_offset + 238] = 83;
    frame[bootp_offset + 239] = 99;
    frame[bootp_offset + 240] = 53;
    frame[bootp_offset + 241] = 1;
    frame[bootp_offset + 242] = message;
    frame[bootp_offset + 243] = 255;
    resize_payload(244);
}

static void expect_empty(size_t length) {
    const ard_wifi_frame_info_t info = ard_wifi_classify_frame(frame, length);
    assert(!info.arp && !info.dhcp && info.dhcp_type == 0 && info.xid == 0);
    ++checks;
}

static void expect_dhcp(uint8_t type) {
    const ard_wifi_frame_info_t info = ard_wifi_classify_frame(frame, frame_length);
    assert(info.dhcp && !info.arp && info.dhcp_type == type && info.xid == 0x1234abcd);
    ++checks;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    const uint8_t types[] = {1, 2, 3, 5, 6};
    for (size_t i = 0; i < sizeof(types); ++i) {
        packet(0, 5, types[i]);
        expect_dhcp(types[i]);
    }
    packet(1, 5, 2); expect_dhcp(2);
    packet(2, 5, 5); expect_dhcp(5);
    packet(0, 6, 3); expect_dhcp(3); /* IPv4 header options. */
    packet(0, 5, 1); put16(ip_offset + 6, 0x4000); expect_dhcp(1); /* DF is valid. */
    packet(0, 5, 1);
    for (size_t length = 0; length < frame_length; ++length) expect_empty(length);
    const ard_wifi_frame_info_t null_info = ard_wifi_classify_frame(NULL, frame_length);
    assert(!null_info.dhcp && !null_info.arp); ++checks;
    packet(0, 5, 1); frame[ip_offset] = 0x44; expect_empty(frame_length);
    packet(0, 5, 1); frame[ip_offset] = 0x65; expect_empty(frame_length);
    packet(0, 5, 1); frame[ip_offset] = 0x4f; expect_empty(frame_length);
    packet(0, 5, 1); put16(ip_offset + 2, 27); expect_empty(frame_length);
    packet(0, 5, 1); put16(ip_offset + 2, 399); expect_empty(frame_length);
    packet(0, 5, 1); frame[ip_offset + 9] = 6; expect_empty(frame_length);
    packet(0, 5, 1); put16(ip_offset + 6, 0x2000); expect_empty(frame_length);
    packet(0, 5, 1); put16(ip_offset + 6, 1); expect_empty(frame_length);
    packet(0, 5, 1); put16(ip_offset + 6, 0x8000); expect_empty(frame_length);
    packet(0, 5, 1); put16(udp_offset, 123); expect_empty(frame_length);
    packet(0, 5, 1); put16(udp_offset + 2, 68); expect_empty(frame_length);
    packet(0, 5, 1); put16(udp_offset + 4, 247); expect_empty(frame_length);
    packet(0, 5, 1); put16(udp_offset + 4, 350); expect_empty(frame_length);
    packet(0, 5, 1); frame[bootp_offset] = 3; expect_empty(frame_length);
    packet(0, 5, 1); frame[bootp_offset + 2] = 8; expect_empty(frame_length);
    packet(0, 5, 1); frame[bootp_offset + 239] = 0; expect_empty(frame_length);
    packet(3, 5, 1); expect_empty(frame_length);
    packet(2, 5, 1); expect_empty(17); expect_empty(21);

    packet(0, 5, 1); frame[bootp_offset + 240] = 255; resize_payload(241); expect_dhcp(0);
    packet(0, 5, 1);
    const uint8_t padded[] = {0, 12, 2, 'o', 'k', 0, 53, 1, 2, 0, 255};
    memcpy(frame + bootp_offset + 240, padded, sizeof(padded));
    resize_payload(240 + sizeof(padded)); expect_dhcp(2);
    packet(0, 5, 1); resize_payload(241); expect_empty(frame_length); /* Missing option length. */
    packet(0, 5, 1); resize_payload(242); expect_empty(frame_length); /* Missing option data. */
    packet(0, 5, 1); resize_payload(243); expect_empty(frame_length); /* Missing end marker. */
    packet(0, 5, 1); resize_payload(240); expect_empty(frame_length);
    packet(0, 5, 1); frame[bootp_offset + 241] = 0; expect_empty(frame_length);
    packet(0, 5, 1); frame[bootp_offset + 241] = 2; expect_empty(frame_length);
    packet(0, 5, 0); expect_empty(frame_length);
    packet(0, 5, 1);
    const uint8_t duplicate[] = {53, 1, 1, 53, 1, 2, 255};
    memcpy(frame + bootp_offset + 240, duplicate, sizeof(duplicate));
    resize_payload(240 + sizeof(duplicate)); expect_empty(frame_length);
    packet(0, 5, 1);
    const uint8_t truncated[] = {53, 1, 1, 12, 9, 0, 255};
    memcpy(frame + bootp_offset + 240, truncated, sizeof(truncated));
    resize_payload(240 + sizeof(truncated)); expect_empty(frame_length);
    packet(0, 5, 1); frame[bootp_offset + 244] = 12; resize_payload(245);
    expect_dhcp(1); /* Padding after end ignored. */

    memset(frame, 0, sizeof(frame)); put16(12, 0x0806);
    put16(14, 1); put16(16, 0x0800); frame[18] = 6; frame[19] = 4;
    const ard_wifi_frame_info_t arp = ard_wifi_classify_frame(frame, 42);
    assert(arp.arp && !arp.dhcp && !arp.xid); ++checks;
    expect_empty(41);
    frame[18] = 255; expect_empty(42);
    memset(frame, 0, sizeof(frame)); put16(12, 0x86dd); expect_empty(sizeof(frame));
    printf("Wi-Fi DHCP frame classifier: %u checks passed\n", checks);
    return 0;
}
