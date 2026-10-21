#ifndef CONFIG_MODE_H
#define CONFIG_MODE_H

#include <Arduino.h>

#ifndef BLUE
#define BLUE 0
#define GREEN 1
#define RED 2
#endif

/**
 * @brief Initialize configuration mode: load saved preferences for the web UI,
 * start WiFi softAP ("clock1" at 192.168.199.1), start captive portal DNS
 * server, mDNS responder ("clock.local"), and HTTP web server.
 */
void config_mode_init(void);

/**
 * @brief Process pending DNS and HTTP client requests in configuration mode.
 * Call this repeatedly from loop() when in configuration mode.
 */
void process_config_mode(void);

#endif // CONFIG_MODE_H
