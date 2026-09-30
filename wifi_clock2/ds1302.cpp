#include "ds1302.h"
#include <string.h>

static uint8_t g_cePin  = 5;
static uint8_t g_clkPin = 6;
static uint8_t g_datPin = 7;

static const uint16_t daysBeforeMonth[] = {
  0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
};

time_t timegm_utc(const struct tm *t) {
  int year = t->tm_year + 1900;
  int mon  = t->tm_mon;

  long leapDays = (year - 1) / 4 - (year - 1) / 100 + (year - 1) / 400 - 477;
  long days = (year - 1970) * 365L + leapDays + daysBeforeMonth[mon];

  bool isLeap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
  if (isLeap && mon > 1) {
    days += 1;
  }
  days += (t->tm_mday - 1);

  return (time_t)(days * 86400L + t->tm_hour * 3600L + t->tm_min * 60L + t->tm_sec);
}

static inline uint8_t bcd2bin(uint8_t val) { return val - 6 * (val >> 4); }
static inline uint8_t bin2bcd(uint8_t val) { return val + 6 * (val / 10); }

static inline void ds1302_bus_delay()
{
  // Half-clock cycle delay (~100-150 kHz clock rate)
  delayMicroseconds(4);
}

static void ds1302_sendCmd(uint8_t cmd)
{
  pinMode(g_datPin, OUTPUT);
  for (int i = 0; i < 8; i++) {
    digitalWrite(g_datPin, (cmd & (1 << i)) ? HIGH : LOW);
    ds1302_bus_delay();
    digitalWrite(g_clkPin, HIGH);
    ds1302_bus_delay();
        
    // Handover: Release bus to high-Z before SCLK drops on read
    // commands (cmd bit 0 == 1)
    if (i == 7 && (cmd & 0x01)) {
      pinMode(g_datPin, INPUT);
    }
        
    digitalWrite(g_clkPin, LOW);
    ds1302_bus_delay();
  }
}

static void ds1302_writeData(uint8_t data)
{
  pinMode(g_datPin, OUTPUT);
  for (int i = 0; i < 8; i++) {
    digitalWrite(g_datPin, (data & (1 << i)) ? HIGH : LOW);
    ds1302_bus_delay();
    digitalWrite(g_clkPin, HIGH);
    ds1302_bus_delay();
    digitalWrite(g_clkPin, LOW);
    ds1302_bus_delay();
  }
}

static uint8_t ds1302_readData()
{
  uint8_t val = 0;
  for (int i = 0; i < 8; i++) {
    ds1302_bus_delay();
    if (digitalRead(g_datPin)) {
      val |= (1 << i);
    }
    digitalWrite(g_clkPin, HIGH);
    ds1302_bus_delay();
    digitalWrite(g_clkPin, LOW);
  }
  return val;
}

void ds1302_writeRegister(uint8_t reg, uint8_t val)
{
  digitalWrite(g_cePin, HIGH);
  ds1302_bus_delay();
  ds1302_sendCmd(reg);
  ds1302_writeData(val);
  digitalWrite(g_cePin, LOW);
  ds1302_bus_delay();
}

uint8_t ds1302_readRegister(uint8_t reg)
{
  digitalWrite(g_cePin, HIGH);
  ds1302_bus_delay();
  ds1302_sendCmd(reg);
  uint8_t val = ds1302_readData();
  digitalWrite(g_cePin, LOW);
  ds1302_bus_delay();
  return val;
}

void ds1302_forceOscillatorOn()
{
  // Disable Write Protect (Register 0x8E = 0x00)
  ds1302_writeRegister(0x8E, 0x00);

  // Read Seconds register (0x81)
  uint8_t secReg = ds1302_readRegister(0x81);

  // If CH bit (bit 7) is 1, clear CH bit to force crystal oscillator ON
  if (secReg & 0x80) {
    Serial.println("[DS1302] Oscillator HALTED (CH=1). Clearing CH bit to start crystal...");
    ds1302_writeRegister(0x80, secReg & 0x7F);
  }
}

bool ds1302_disableTrickleCharge()
{
  // Disable Write Protect (Register 0x8E = 0x00)
  ds1302_writeRegister(0x8E, 0x00);

  // Write 0x00 to Trickle Charge Register (0x90) to disable trickle charging
  ds1302_writeRegister(0x90, 0x00);

  // Read back Trickle Charge Register (0x91) to verify it is 0x00
  uint8_t val = ds1302_readRegister(0x91);
  if (val != 0x00) {
    Serial.print("[DS1302] WARNING: Trickle charge disable failed! Read back 0x");
    Serial.println(val, HEX);
    return false;
  }

  return true;
}

void ds1302_init(uint8_t cePin, uint8_t clkPin, uint8_t datPin)
{
  g_cePin  = cePin;
  g_clkPin = clkPin;
  g_datPin = datPin;

  pinMode(g_cePin, OUTPUT);
  pinMode(g_clkPin, OUTPUT);
  pinMode(g_datPin, INPUT);
  digitalWrite(g_cePin, LOW);
  digitalWrite(g_clkPin, LOW);

  // Disable WP and ensure oscillator is un-halted on cold boot
  ds1302_forceOscillatorOn();

  // Explicitly disable trickle charging (writes 0x00 to reg 0x90, reads back reg 0x91, checks value is 0x00)
  ds1302_disableTrickleCharge();
}

bool ds1302_readBurst(struct tm *timeinfo, uint8_t rawOut[8])
{
  digitalWrite(g_clkPin, LOW);
  digitalWrite(g_cePin, HIGH);
  ds1302_bus_delay();

  // Send read burst command (0xBF) and release DAT pin on bit 7
  ds1302_sendCmd(0xBF);

  uint8_t raw[8];
  for (int i = 0; i < 8; i++) {
    raw[i] = ds1302_readData();
  }

  digitalWrite(g_cePin, LOW);
  ds1302_bus_delay();

  if (rawOut) {
    memcpy(rawOut, raw, 8);
  }

  // Check for open/floating bus (all 0xFF) or grounded line (all 0x00)
  if ((raw[0] == 0xFF && raw[1] == 0xFF && raw[2] == 0xFF) ||
      (raw[0] == 0x00 && raw[1] == 0x00 && raw[2] == 0x00 && raw[6] == 0x00)) {
    return false;
  }

  // Check Clock Halt bit (bit 7 of Seconds)
  if (raw[0] & 0x80) {
    return false; // Oscillator is halted
  }

  timeinfo->tm_sec   = bcd2bin(raw[0] & 0x7F);
  timeinfo->tm_min   = bcd2bin(raw[1] & 0x7F);
  timeinfo->tm_hour  = bcd2bin(raw[2] & 0x3F);
  timeinfo->tm_mday  = bcd2bin(raw[3] & 0x3F);
  timeinfo->tm_mon   = bcd2bin(raw[4] & 0x1F) - 1;
  timeinfo->tm_wday  = bcd2bin(raw[5] & 0x07) - 1;
  timeinfo->tm_year  = bcd2bin(raw[6]) + 100;
  timeinfo->tm_isdst = 0;

  return true;
}

void ds1302_writeBurst(const struct tm *timeinfo)
{
  // 1. Disable write protect (Command 0x8E = 0x00)
  ds1302_writeRegister(0x8E, 0x00);

  // 2. Transmit clock burst (Command 0xBE)
  digitalWrite(g_cePin, HIGH);
  ds1302_bus_delay();
  ds1302_sendCmd(0xBE);

  ds1302_writeData(bin2bcd(timeinfo->tm_sec) & 0x7F); // Bit 7 = 0 starts oscillator
  ds1302_writeData(bin2bcd(timeinfo->tm_min));
  ds1302_writeData(bin2bcd(timeinfo->tm_hour) & 0x3F);
  ds1302_writeData(bin2bcd(timeinfo->tm_mday));
  ds1302_writeData(bin2bcd(timeinfo->tm_mon + 1));
  ds1302_writeData(bin2bcd(timeinfo->tm_wday + 1));
  ds1302_writeData(bin2bcd(timeinfo->tm_year % 100));
  ds1302_writeData(0x00); // Keep WP disabled so RTC ticks cleanly

  digitalWrite(g_cePin, LOW);
  ds1302_bus_delay();
}
