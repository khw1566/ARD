#ifndef ARD_WIFI_STATION_MAC_H
#define ARD_WIFI_STATION_MAC_H
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Call from the station-connected event before the default handler starts
 * DHCP. Repair the host interface identity only while the interface is down;
 * never change the radio MAC or mutate a live network interface.
 */
esp_err_t ard_wifi_sync_station_mac(void);

#ifdef __cplusplus
}
#endif
#endif
