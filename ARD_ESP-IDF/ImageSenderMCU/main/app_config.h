#pragma once
// Initial PC backend host; the /network settings page overrides it in NVS.
#define APP_DEFAULT_SERVER "10.115.5.12"
#define APP_NET_ROLE "image"

#define APP_HTTP_PORT 5000      // PC Flask: /upload, /api/imagesendermcu/config, /api/log, /api/heartbeat
#define APP_IMG_UDP_PORT 5001   // PC image receiver (ARD image protocol v2)
#define APP_CMD_TCP_PORT 5003   // capture trigger from PC ("c", "capture", "ping")

#define APP_CONFIG_INTERVAL_MS 3000
#define APP_HEARTBEAT_INTERVAL_MS 10000 // PC marks a board offline after 30 s

// Camera defaults (overridden by the PC config poll).
#define APP_DEFAULT_RESOLUTION "SVGA"
#define APP_DEFAULT_QUALITY 12

// Reliable UDP transfer tuning (see img_tx.c).
#define APP_TX_WINDOW_CHUNKS 32   // chunks sent between POLLs (flow control)
#define APP_TX_POLL_TIMEOUT_MS 60 // wait for STATUS after each POLL
#define APP_TX_POLL_RETRIES 8     // POLLs without STATUS before giving up
#define APP_TX_BASE_DEADLINE_MS 3000
#define APP_TX_ENOMEM_WAIT_MS 2   // back-off while the Wi-Fi TX queue is full
#define APP_TX_ENOMEM_LIMIT_MS 500
