// Wi-Fi, the time and HTTPS requests.
#ifndef NET_H
#define NET_H

#include <stddef.h>

// Join the Wi-Fi network from menuconfig. Returns 0 once connected.
int net_connect(int timeout_ms);

// Set the clock over NTP (and keep it set). Returns 0 once synced.
int net_sync_time(int timeout_ms);

// GET `url` into buf (NUL-terminated). Returns the HTTP status, or -1.
int net_get(const char *url, char *buf, size_t size);

void net_stop(void);

#endif
