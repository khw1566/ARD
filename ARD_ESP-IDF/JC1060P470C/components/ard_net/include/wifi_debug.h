#pragma once
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_ARD_WIFI_DIAGNOSTICS
void wifi_debug_init(void);
void wifi_debug_poll(const char *context);
void wifi_debug_dump(const char *context);
#else
static inline void wifi_debug_init(void) {}
static inline void wifi_debug_poll(const char *context) { (void)context; }
static inline void wifi_debug_dump(const char *context) { (void)context; }
#endif

#ifdef __cplusplus
}
#endif
