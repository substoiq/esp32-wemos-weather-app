#pragma once
#include <stdbool.h>

void wifi_start(void);                         // start connecting (in the background)
bool wifi_wait_connected(int timeout_ms);      // wait until there is an IP address
bool wifi_is_connected(void);
