#ifndef AIP33628_H
#define AIP33628_H

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#endif
#if defined(ESP32) || defined(ESP_PLATFORM)
#include <esp_timer.h>
#elif !defined(UNIT_TEST)
#error "This AiP33628 driver is designed for ESP32 and requires esp_timer.h"
#endif

#define NUM_DRIVERS         2

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize GPIOs for AIP33628 drivers and start the periodic
 * 1.25ms scan timer (200 Hz LED refresh rate).
 */
void aip33628_init(uint8_t driver1_clock_pin, uint8_t driver1_data_pin,
                   uint8_t driver2_clock_pin, uint8_t driver2_data_pin);

/**
 * @brief Set current magnitude for all frames (0 to 15).
 * @param is_val Current level (0 = lowest, 15 = highest).
 */
void aip33628_set_current(uint8_t is_val);

/**
 * @brief Set entire 16-bit segment bitmask for a driver and phase.
 * @param driver_index Driver index (0 or 1).
 * @param phase_index Scan phase index (0 to 3).
 * @param seg_value 16-bit mask representing SEG15..SEG0.
 */
void aip33628_set_seg(uint8_t driver_index, uint8_t phase_index,
                      uint16_t seg_value);

/**
 * @brief Enable an individual LED segment.
 * @param driver_index Driver index (0 or 1).
 * @param phase_index Scan phase index (0 to 3).
 * @param seg_bit Bit position within segment (0 to 15).
 */
void aip33628_ena_led(uint8_t driver_index, uint8_t phase_index,
                      uint8_t seg_bit);

/**
 * @brief Disable an individual LED segment.
 * @param driver_index Driver index (0 or 1).
 * @param phase_index Scan phase index (0 to 3).
 * @param seg_bit Bit position within segment (0 to 15).
 */
void aip33628_dis_led(uint8_t driver_index, uint8_t phase_index,
                      uint8_t seg_bit);

/**
 * @brief Turn off all LEDs across all drivers and phases.
 */
void aip33628_clear_all(void);

#define aip33629_clear_all aip33628_clear_all

#ifdef __cplusplus
}
#endif

#endif // AIP33628_H
