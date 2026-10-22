#pragma once
#include <stdbool.h>
#include "third_party/qrcodegen.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ARD_QR_BYTES ard_qrcodegen_BUFFER_LEN_FOR_VERSION(6)
#define ARD_SETUP_URL "http://192.168.4.1/network"
typedef struct { uint8_t modules[ARD_QR_BYTES]; } ard_setup_qr_t;
// Generated once at startup, then immutable for serial and display readers.
void ard_setup_qr_init(const char *ssid, const char *password);
const ard_setup_qr_t *ard_setup_qr(bool wifi);
void ard_setup_qr_print(void);

#ifdef __cplusplus
}
#endif
