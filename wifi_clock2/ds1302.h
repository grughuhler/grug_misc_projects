#ifndef DS1302_H
#define DS1302_H

#include <Arduino.h>
#include <time.h>

/**
 * @brief Initialize the DS1302 RTC GPIO pins and force the oscillator
 * ON (clearing CH bit and disabling WP).
 *
 * @param cePin  Chip Enable (RST) pin
 * @param clkPin Serial Clock (SCLK) pin
 * @param datPin Serial Data (I/O) pin
 */
void ds1302_init(uint8_t cePin, uint8_t clkPin, uint8_t datPin);

/**
 * @brief Forces write-protection OFF and clears Clock Halt (CH) bit
 * to start crystal oscillator.
 */
void ds1302_forceOscillatorOn();

/**
 * @brief Explicitly disables trickle charge on DS1302 (writes 0x00 to reg 0x90) and reads back (reg 0x91) to confirm.
 * @return true if register readback equals 0x00, false otherwise
 */
bool ds1302_disableTrickleCharge();


/**
 * @brief Write a single byte to a DS1302 register.
 *
 * @param reg Register write address (e.g. 0x8E for WP, 0x80 for Seconds)
 * @param val Value byte to write
 */
void ds1302_writeRegister(uint8_t reg, uint8_t val);

/**
 * @brief Read a single byte from a DS1302 register.
 *
 * @param reg Register read address (e.g. 0x81 for Seconds, 0x8F for WP)
 * @return Byte read from register
 */
uint8_t ds1302_readRegister(uint8_t reg);

/**
 * @brief Reads time registers from DS1302 in burst mode.
 * @param timeinfo Pointer to struct tm to populate with parsed date/time
 * @param rawOut Optional 8-byte buffer to receive raw register bytes
 * @return true if valid data read, false if communications error or oscillator halted (CH bit = 1)
 */
bool ds1302_readBurst(struct tm *timeinfo, uint8_t rawOut[8] = nullptr);

/**
 * @brief Writes date/time to DS1302 in burst mode, clearing CH bit and enabling oscillator.
 * @param timeinfo Pointer to struct tm containing UTC date/time to write
 */
void ds1302_writeBurst(const struct tm *timeinfo);

/**
 * @brief Portable conversion from UTC struct tm to time_t Unix epoch.
 * @param t Pointer to struct tm representing UTC time
 * @return time_t Epoch seconds since Jan 1 1970
 */
time_t timegm_utc(const struct tm *t);

#endif // DS1302_H
