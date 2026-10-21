#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <sys/time.h>
#include <esp_sntp.h>
#include <Preferences.h>
#include "ds1302.h"
#include "aip33628.h"
#include "config_mode.h"

// ESP32 is a 3.3V device with 3.3V signaling
#define VCC_ESP32 3.3f

// There are 33 RGB LEDs on the clock board.  Each RGB LED contains three
// LEDs: a blue one, a green one, and a red one.  Below, there is a table
// of all of the RGB LEDs.  These are offsets that select the color in LED
// APIs.
#define BLUE 0
#define GREEN 1
#define RED 2

// ESP32-C3 SuperMini Pin Definitions for HU-058 Board

#define PHOTO_RES_PIN       0 // ADC input from photoresistor
#define NTC_PIN             1 // ADC input from thermistor
#define BUZZER_PIN         10 // Output for buzzer

#define AIP33628_1_CLK_PIN  5 // Output to 1st AIP33628
#define AIP33628_1_DATA_PIN 2 // Output to 1st AIP33628
#define AIP33628_2_CLK_PIN  4 // Output to 2nd AIP33628
#define AIP33628_2_DATA_PIN 3 // Output to 2nd AIP33628

#define DS1302_CE_PIN  6  // Output to DS1302
#define DS1302_CLK_PIN 9  // Output to DS1302
#define DS1302_DAT_PIN 7  // Bi-directional DS1302 data pin.

#define BUTTON1_PIN 21 // Input to ESP32 (internal pullup)
#define BUTTON2_PIN 20 // Input to ESP32 (internal pullup)

// base values for display_digit
#define MIN_1S 0
#define MIN_10S 7
#define HOUR_1S 14
#define HOUR_10S 21

// values for "what" in display_other
#define COLON 0
#define DASH 31
#define DEGREE 32
#define AM_IND 28

#define MODE_TIME 0
#define MODE_TEMPERATURE 1
#define MODE_BLANK 2

uint8_t mode = MODE_TIME;
bool in_config_mode = false;

int16_t t_fah = 0; // Current temperature

// Preferences & Wi-Fi Settings
Preferences preferences;
String ssid = "";
String password = "";
String timeZone = "CST6CDT,M3.2.0,M11.1.0";
bool dst_enabled = true;
bool use_24h = false;
uint8_t clock_color = GREEN;

// NTP Servers
bool use_default_ntp = true;
String active_ntp_server1 = "pool.ntp.org";
String active_ntp_server2 = "time.nist.gov";

// Global State & Alarms
bool ntp_time_valid = false;
time_t last_successful_ntp_epoch = 0;

time_t next_sync_epoch = 0;
time_t next_minute_print_epoch = 0;

volatile bool sntpSyncTriggered = false;

// Non-blocking NTP Sync State Machine
enum NtpSyncState {
  NTP_STATE_IDLE,
  NTP_STATE_CONNECTING_WIFI,
  NTP_STATE_WAIT_BEFORE_SNTP,
  NTP_STATE_WAITING_SNTP
};

NtpSyncState ntp_sync_state = NTP_STATE_IDLE;
unsigned long ntp_state_start_millis = 0;
unsigned long ntp_last_dot_millis = 0;

// This table lists all 33 RGB LEDs on the clock board and gives
// information on how to access them.
const struct {
  uint8_t driver;  // Which AIP33628 (0 or 1)
  uint8_t phase;   // COM pair sequence index
  uint8_t segment; // Which segment (gives blue lED)
} led_list[33] = {
  {1, 1, 13},
  {1, 1, 10},
  {1, 1, 7},
  {1, 1, 4},
  {1, 0, 13},
  {1, 0, 10},
  {1, 0, 7},
  {1, 3, 13},
  {1, 3, 10},
  {1, 3, 7},
  {1, 3, 4},
  {1, 2, 13},
  {1, 2, 10},
  {1, 2, 7},
  {0, 1, 13},
  {0, 1, 10},
  {0, 1, 7},
  {0, 1, 4},
  {0, 0, 13},
  {0, 0, 10},
  {0, 0, 7},
  {0, 3, 13},
  {0, 3, 10},
  {0, 3, 7},
  {0, 3, 4},
  {0, 2, 13},
  {0, 2, 10},
  {0, 2, 7},
  {0, 3, 1},
  {0, 1, 1},
  {0, 0, 1},
  {1, 3, 1},
  {1, 1, 1}
};

// This table lists the LEDs needed to form 0-9 and A-Z
// as well as can be...
const uint8_t numbers_letters[36][8] = {
  {0, 1, 2, 3, 4, 5, 255, 0}, // 0
  {1, 2, 255, 0, 0, 0, 0, 0}, // 1
  {0, 1, 6, 4, 3, 255, 0, 0}, // 2
  {0, 1, 2, 3, 6, 255, 0, 0}, // 3
  {5, 1, 6, 2, 255, 0, 0, 0}, // 4
  {0, 5, 6, 2, 3, 255, 0, 0}, // 5
  {0, 5, 4, 3, 2, 6, 255, 0}, // 6
  {0, 1, 2, 255, 0, 0, 0, 0}, // 7
  {0, 1, 2, 3, 4, 5, 6, 255}, // 8
  {6, 5, 0, 1, 2, 3, 255, 0}, // 9
  {0, 5, 1, 6, 4, 2, 255, 0}, // A (10)
  {5, 4, 3, 2, 6, 255, 0, 0}, // B (11)
  {0, 5, 4, 3, 255, 0, 0, 0}, // C (12)
  {1, 2, 3, 4, 6, 255, 0, 0}, // D (13)
  {0, 5, 4, 3, 6, 255, 0, 0}, // E (14)
  {0, 5, 6, 4, 255, 0, 0, 0}, // F (15)
  {0, 1, 5, 6, 2, 3, 255, 0}, // g (16)
  {1, 2, 4, 5, 6, 255, 0, 0}, // H
  {1, 2, 255, 0, 0, 0, 0, 0}, // I
  {1, 2, 3, 4, 255, 0, 0, 0}, // J
  {4, 5, 6, 255, 0, 0, 0, 0}, // K (poor)
  {3, 4, 5, 255, 0, 0, 0, 0}, // L
  {0, 1, 2, 4, 5, 255, 0, 0}, // M (poor)
  {2, 4, 6, 255, 0, 0, 0, 0}, // n
  {0, 1, 2, 3, 4, 5, 255, 0}, // O
  {0, 1, 6, 4, 5, 255, 0, 0}, // P
  {0, 1, 5, 6, 2, 255, 0, 0}, // q
  {6, 4, 255, 0, 0, 0, 0, 0}, // r
  {0, 5, 6, 2, 3, 255, 0, 0}, // S
  {0, 1, 2, 255, 0, 0, 0, 0}, // t (poor)
  {1, 2, 3, 4, 5, 255, 0, 0}, // U
  {2, 3, 4, 255, 0, 0, 0, 0}, // v (poor)
  {2, 3, 4, 6, 255, 0, 0, 0}, // w (poor)
  {0, 1, 5, 6, 255, 0, 0, 0}, // x (poor)
  {1, 5, 6, 2, 255, 0, 0, 0}, // Y (poor)
  {0, 1, 6, 4, 3, 255, 0, 0}  // Z
};

// Display a digit 0-F on one of the 4 7-segment blocks.
void display_digit(uint8_t base, uint8_t digit, uint8_t color)
{
  int i = 0;
  uint8_t c;

  if (digit >= sizeof(numbers_letters)/sizeof(numbers_letters[0]))
     digit = 0;
  while ((c = numbers_letters[digit][i++]) != 255) {
    aip33628_ena_led(led_list[c + base].driver,
                     led_list[c + base].phase,
                     led_list[c + base].segment + color);
  }
}

// Display "-" in position given by base
void display_minus(uint8_t base, uint8_t color)
{
  aip33628_ena_led(led_list[6 + base].driver,
                   led_list[6 + base].phase,
                   led_list[6 + base].segment + color);
}

// Enable an LED that is not part of a 7-segment character.
void display_other(uint8_t what, uint8_t color)
{
  if (what > 32) return;

  if (what == COLON) {
    aip33628_ena_led(led_list[29].driver,
                     led_list[29].phase,
                     led_list[29].segment + color);
    aip33628_ena_led(led_list[30].driver,
                     led_list[30].phase,
                     led_list[30].segment + color);
  } else {
    aip33628_ena_led(led_list[what].driver,
                     led_list[what].phase,
                     led_list[what].segment + color);       
  }
}

// Displays "HI" on the LEDs.
void display_hi(void)
{
  uint8_t leds[] = {19, 18, 20, 16, 15, 11, 12};

  for (uint8_t i = 0; i < sizeof(leds)/sizeof(leds[0]); i++)
    aip33628_ena_led(led_list[leds[i]].driver,
                     led_list[leds[i]].phase,
                     led_list[leds[i]].segment + clock_color);    
}

// Displays " BAD" on the LEDs in red
void display_bad(void)
{
  aip33628_clear_all();
  display_digit(HOUR_1S, 11, RED); // B
  display_digit(MIN_10S, 10, RED); // A
  display_digit(MIN_1S, 13, RED);  // D
}


#define LETTER(c) (c - 'A' + 10)

// Displays "COnF" on the LEDs in blue
void display_conf(void)
{
  aip33628_clear_all();
  display_digit(HOUR_10S, LETTER('C'), BLUE);
  display_digit(HOUR_1S, LETTER('O'), BLUE);
  display_digit(MIN_10S, LETTER('N'), BLUE);
  display_digit(MIN_1S, LETTER('F'), BLUE);
}

// Load configuration from ESP32 Preferences
bool loadPreferences(void)
{
  preferences.begin("wifi_clock", true); // read-only
  String saved_ssid = preferences.getString("ssid", "");
  String saved_password = preferences.getString("password", "");
  String saved_timezone = preferences.getString("timezone", "");
  bool saved_use_24h = preferences.getBool("use_24h", false);
  bool saved_dst_enable = preferences.getBool("dst", true);
  clock_color = preferences.getUChar("color", GREEN);
  bool saved_use_default_ntp = preferences.getBool("ntp_default", true);
  String saved_ntp_server1 = preferences.getString("ntp_server1", "pool.ntp.org");
  String saved_ntp_server2 = preferences.getString("ntp_server2", "time.nist.gov");
  preferences.end();

  if (saved_ssid.length() == 0 || saved_timezone.length() == 0) {
    return false;
  }

  ssid = saved_ssid;
  password = saved_password;
  timeZone = saved_timezone;
  use_24h = saved_use_24h;
  dst_enabled = saved_dst_enable;
  use_default_ntp = saved_use_default_ntp;

  if (use_default_ntp) {
    active_ntp_server1 = "pool.ntp.org";
    active_ntp_server2 = "time.nist.gov";
  } else {
    active_ntp_server1 = saved_ntp_server1;
    active_ntp_server2 = saved_ntp_server2;
  }

  return true;
}

// =========================================================================
// Time Validity & Alarm Calculation Helpers
// =========================================================================

void updateNtpTimeValidStatus(time_t currentEpoch)
{
  if (last_successful_ntp_epoch > 0 &&
      (currentEpoch - last_successful_ntp_epoch) <= 45000) {
    ntp_time_valid = true;
  } else {
    ntp_time_valid = false;
  }
}

time_t calculateNextSyncEpoch(time_t currentEpoch, bool syncSuccess)
{
  static int failCount = 0; // consecutive failures

  if (!syncSuccess) {
    failCount++;
    if (failCount <= 10) {
      return currentEpoch + 60; // Retry in 1 minute
    } else {
      return currentEpoch + 3600; // After 10 failures, retry hourly
    }
  }

  // Sync succeeded – reset failure counter
  failCount = 0;

  time_t defaultNext = currentEpoch + (6 * 3600); // 6 hours

  struct tm tmLocal;
  localtime_r(&currentEpoch, &tmLocal);

  struct tm tm205 = tmLocal;
  tm205.tm_hour  = 2;
  tm205.tm_min   = 5;
  tm205.tm_sec   = 0;
  tm205.tm_isdst = -1; // Auto-determine DST for 2:05 AM local time

  time_t epoch205 = mktime(&tm205);

  if (epoch205 <= currentEpoch) {
    tm205.tm_mday += 1;
    epoch205 = mktime(&tm205);
  }

  if (defaultNext > epoch205) {
    return epoch205;
  }

  return defaultNext;
}

void printScheduledTime(const char* label, time_t epoch)
{
  struct tm tmInfo;
  localtime_r(&epoch, &tmInfo);
  char timeStr[64];
  strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S %Z", &tmInfo);
  Serial.printf("%s %s\n", label, timeStr);
}

bool get_local_time(uint8_t *hours, uint8_t *minutes, bool *is_am)
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 0)) {
    return false;
  }

  uint8_t h24 = timeinfo.tm_hour;
  *minutes = timeinfo.tm_min;

  if (use_24h) {
    *hours = h24;
    *is_am = false;
  } else {
    if (h24 == 0) {
      *hours = 12;
      *is_am = true;
    } else if (h24 < 12) {
      *hours = h24;
      *is_am = true;
    } else if (h24 == 12) {
      *hours = 12;
      *is_am = false;
    } else {
      *hours = h24 - 12;
      *is_am = false;
    }
  }

  return true;
}

void printLocalTime()
{
  uint8_t hours = 0;
  uint8_t minutes = 0;
  bool is_am = true;

  if (!get_local_time(&hours, &minutes, &is_am)) {
    Serial.println("time invalid");
    return;
  }

  struct tm timeinfo;
  getLocalTime(&timeinfo, 0);

  char dateStr[64];
  char tzStr[16];
  strftime(dateStr, sizeof(dateStr), "%A, %B %d, %Y", &timeinfo);
  strftime(tzStr, sizeof(tzStr), "%Z", &timeinfo);

  if (use_24h) {
    Serial.printf("%s - %02d:%02d %s [ntp_time_valid: %s]\n",
                  dateStr,
                  hours,
                  minutes,
                  tzStr,
                  ntp_time_valid ? "true" : "false");
  } else {
    Serial.printf("%s - %d:%02d %s %s [ntp_time_valid: %s]\n",
                  dateStr,
                  hours,
                  minutes,
                  is_am ? "AM" : "PM",
                  tzStr,
                  ntp_time_valid ? "true" : "false");
  }

  aip33628_clear_all();
  display_other(COLON, clock_color);
  display_digit(MIN_1S, minutes % 10, clock_color);
  display_digit(MIN_10S, minutes / 10, clock_color);
  display_digit(HOUR_1S, hours % 10, clock_color);
  if (hours > 9) display_digit(HOUR_10S, hours / 10, clock_color);
  if (!use_24h && is_am) display_other(AM_IND, clock_color);
  if (!ntp_time_valid)
    display_other(DEGREE, clock_color);
}

// ===========================================================
// System Time & Non-Blocking NTP Sync Logic
// ===========================================================

bool syncSystemFromRtc()
{
  struct tm rtcTm;
  uint8_t raw[8];
  bool valid = ds1302_readBurst(&rtcTm, raw);

  Serial.printf("[RTC Raw Dump] %02X %02X %02X %02X %02X %02X %02X %02X\n",
                raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7]);

  if (!valid || rtcTm.tm_year < 124) { 
    if (raw[0] & 0x80) {
      Serial.println("[RTC] Oscillator is HALTED (CH bit = 1).");
    } else {
      Serial.println("[RTC] Communication error or invalid pre-2024 data.");
    }
    return false;
  }

  time_t rtcEpoch = timegm_utc(&rtcTm);
  struct timeval tv = { .tv_sec = rtcEpoch, .tv_usec = 0 };
  settimeofday(&tv, NULL);

  Serial.printf("[RTC] System clock seeded from RTC (Epoch: %ld UTC).\n",
                (long)rtcEpoch);
  return true;
}

void timeSyncCallback(struct timeval *tv)
{
  sntpSyncTriggered = true;
  Serial.println("\n[NTP] Packet received. Clock slewing initiated.");
}

void stopWiFi()
{
  Serial.println("[Radio] Powering down Wi-Fi...");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

void startNtpSync(void)
{
  Serial.println("\n[Radio] Powering on Wi-Fi for NTP sync...");
  sntpSyncTriggered = false;

  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.setHostname("clock1");
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

  WiFi.begin(ssid.c_str(), password.c_str());

  ntp_state_start_millis = millis();
  ntp_last_dot_millis = ntp_state_start_millis;
  ntp_sync_state = NTP_STATE_CONNECTING_WIFI;
}

static void finishNtpSync(bool syncOk)
{
  stopWiFi();

  time_t now = time(NULL);

  if (syncOk) {
    last_successful_ntp_epoch = now;
    updateNtpTimeValidStatus(now);

    struct tm rtcTm;
    uint8_t raw[8];
    bool rtcOk = ds1302_readBurst(&rtcTm, raw);

    if (!rtcOk || (raw[0] & 0x80)) {
      Serial.println("[RTC] Oscillator halted or uninitialized. Writing NTP time to DS1302...");
      struct tm utcInfo;
      gmtime_r(&now, &utcInfo);
      ds1302_writeBurst(&utcInfo);
      Serial.println("[RTC] DS1302 started successfully.");
    } else {
      time_t rtcEpoch = timegm_utc(&rtcTm);
      long driftSeconds = labs((long)now - (long)rtcEpoch);
      Serial.printf("[NTP vs RTC] Absolute Delta: %ld seconds.\n", driftSeconds);

      if (driftSeconds > 10) {
        Serial.println("[RTC] Drift > 10s. Overwriting DS1302 registers...");
        struct tm utcInfo;
        gmtime_r(&now, &utcInfo);
        ds1302_writeBurst(&utcInfo);
        Serial.println("[RTC] DS1302 updated.");
      } else {
        Serial.println("[RTC] Discrepancy <= 10s. Update skipped.");
      }
    }

    Serial.print("[NTP] Synchronized Local Time: ");
    if (mode == MODE_TIME) {
      printLocalTime();
    } else {
      Serial.println("(display in non-time mode)");
    }

    next_minute_print_epoch = (now / 60 + 1) * 60;
    printScheduledTime("[Schedule] Next minute print alarm:",
                       next_minute_print_epoch);
  } else {
    updateNtpTimeValidStatus(now);
  }

  next_sync_epoch = calculateNextSyncEpoch(now, syncOk);
  printScheduledTime("[Schedule] Next NTP sync alarm:", next_sync_epoch);
  ntp_sync_state = NTP_STATE_IDLE;
}

void process_ntp_sync(time_t now)
{
  unsigned long curMillis = millis();

  switch (ntp_sync_state) {
    case NTP_STATE_IDLE:
      if (next_sync_epoch > 0 && now >= next_sync_epoch) {
        startNtpSync();
      }
      break;

    case NTP_STATE_CONNECTING_WIFI:
      if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[Radio] Connected (RSSI: %d dBm).\n", WiFi.RSSI());
        ntp_state_start_millis = curMillis;
        ntp_sync_state = NTP_STATE_WAIT_BEFORE_SNTP;
      } else if (curMillis - ntp_state_start_millis >= 18000) {
        Serial.println("\n[Radio] Wi-Fi connection failed. Keeping current system time.");
        finishNtpSync(false);
      } else if (curMillis - ntp_last_dot_millis >= 250) {
        ntp_last_dot_millis = curMillis;
        Serial.print(".");
      }
      break;

    case NTP_STATE_WAIT_BEFORE_SNTP:
      if (sntpSyncTriggered) {
        finishNtpSync(true);
      } else if (curMillis - ntp_state_start_millis >= 500) {
        sntp_restart();
        ntp_state_start_millis = curMillis;
        ntp_sync_state = NTP_STATE_WAITING_SNTP;
      }
      break;

    case NTP_STATE_WAITING_SNTP:
      if (sntpSyncTriggered) {
        finishNtpSync(true);
      } else if (curMillis - ntp_state_start_millis >= 15000) {
        Serial.println("[NTP] Sync timed out.");
        finishNtpSync(false);
      }
      break;
  }
}

#define NTC_R_FIXED     18000.0f // Value of series resistor
#define R_NOMINAL       10000.0  // NTC nominal resistance at 25C
#define B_COEFFICIENT   3950.0   // B-value from datasheet
#define T_NOMINAL       298.15   // 25C in Kelvin

void process_temperature(void)
{
  static uint32_t next_millis = 0;
  uint32_t cur_millis;
  float voltage, r_ntc, t_kelvin;
  
  if ((cur_millis = millis()) > next_millis) {
    next_millis = cur_millis + 1000;
    voltage = analogReadMilliVolts(NTC_PIN)/1000.0f;
    r_ntc = NTC_R_FIXED * voltage / (VCC_ESP32 - voltage);
    // B-parameter equation fortemperature
    t_kelvin = 1.0f / ((1.0f / T_NOMINAL) + (1.0f / B_COEFFICIENT) * log(r_ntc / R_NOMINAL));
    t_fah = (t_kelvin - 273.15f)*1.8f + 32.0f + 0.5;
    Serial.print(voltage);
    Serial.println(" from NTC");
    Serial.print(t_fah);
    Serial.println(" deg F");
  }
}

void process_brightness(void)
{
  static uint32_t next_millis = 0;
  uint32_t cur_millis = millis();
  uint32_t v;

  if (cur_millis >= next_millis) {
    next_millis += 1000;
    v = analogReadMilliVolts(PHOTO_RES_PIN);
    Serial.print(v);
    Serial.println(" mV from photoresistor");
    // You have have to tune for your photoresistor here
    if (v > 2000)
      aip33628_set_current(0); // Room is dark
    else if (v < 1800)
      aip33628_set_current(8); // Room bright (with hysteresis
  }
}

void printTemperature()
{
  aip33628_clear_all();

  if ((t_fah >= 0) && (t_fah < 100)) {
    // normal two-digit positive temperature
    display_digit(HOUR_1S, t_fah/10, clock_color);
    display_digit(MIN_10S, t_fah%10, clock_color);
  } else if ((t_fah >= 100) && (t_fah <= 199)) {
    // Three digit positive temperature
    display_digit(HOUR_10S, 1, clock_color);
    display_digit(HOUR_1S, (t_fah - 100)/10, clock_color);
    display_digit(MIN_10S, (t_fah - 100)%10, clock_color);
  } else if ((t_fah >= -99) && (t_fah < 0)) {
    // Two digit negative temperature
    display_minus(HOUR_10S, clock_color);
    display_digit(HOUR_1S, (-t_fah)/10, clock_color);
    display_digit(MIN_10S, (-t_fah)%10, clock_color);
  } else {
    // Crazy temperature
    display_bad();
  }
}

void process_buttons(void)
{
  static unsigned long btn1_press_start = 0;
  static bool btn1_long_handled = false;
  static bool btn1_last_state = HIGH;

  static unsigned long btn2_press_start = 0;
  static bool btn2_long_handled = false;
  static bool btn2_last_state = HIGH;

  unsigned long currentMillis = millis();

  bool btn1_pressed = (digitalRead(BUTTON1_PIN) == LOW);
  bool btn2_pressed = (digitalRead(BUTTON2_PIN) == LOW);

  // --- Button 1 Handling ---
  if (btn1_pressed) {
    if (btn1_last_state == HIGH) {
      // Button 1 transition HIGH -> LOW (press started)
      btn1_press_start = currentMillis;
      btn1_long_handled = false;
    } else if (!btn1_long_handled && (currentMillis - btn1_press_start >= 5000)) {
      // 5-second hold threshold reached
      btn1_long_handled = true;
      Serial.println("[Button 1] 5-second hold detected! Rebooting into Config Mode...");
      preferences.begin("wifi_clock", false);
      preferences.putBool("force_config", true);
      preferences.end();
      delay(500);
      ESP.restart();
    }
  } else {
    if (btn1_last_state == LOW) {
      // Button 1 transition LOW -> HIGH (released)
      unsigned long pressDuration = currentMillis - btn1_press_start;
      if (!btn1_long_handled && pressDuration >= 50 && pressDuration < 5000) {
        // Brief press of Button 1
        Serial.println("[Button 1] Brief press detected");
        if (mode == MODE_BLANK) {
          mode = MODE_TIME;
        } else {
          mode += 1;
        }
        switch (mode) {
          case MODE_TIME:
            printLocalTime();
            break;
         case MODE_TEMPERATURE:
            printTemperature();
            break;
          case MODE_BLANK:
            aip33628_clear_all();
            break;
          default:
            break;
        }
      }
    }
  }
  btn1_last_state = btn1_pressed ? LOW : HIGH;

  // --- Button 2 Handling ---
  if (btn2_pressed) {
    if (btn2_last_state == HIGH) {
      // Button 2 transition HIGH -> LOW (press started)
      btn2_press_start = currentMillis;
      btn2_long_handled = false;
    } else if (!btn2_long_handled && (currentMillis - btn2_press_start >= 5000)) {
      // 5-second hold threshold reached
      btn2_long_handled = true;
      Serial.println("[Button 2] 5-second hold detected! Rebooting into Config Mode...");
      preferences.begin("wifi_clock", false);
      preferences.putBool("force_config", true);
      preferences.end();
      delay(500);
      ESP.restart();
    }
  } else {
    if (btn2_last_state == LOW) {
      // Button 2 transition LOW -> HIGH (released)
      unsigned long pressDuration = currentMillis - btn2_press_start;
      if (!btn2_long_handled && pressDuration >= 50 && pressDuration < 5000) {
        // Brief press of Button 2 -> cycle color BLUE -> GREEN -> RED -> BLUE
        if (clock_color == BLUE) {
          clock_color = GREEN;
        } else if (clock_color == GREEN) {
          clock_color = RED;
        } else {
          clock_color = BLUE;
        }

        Serial.printf("[Button 2] Brief press detected. Color changed to %s.\n",
                      clock_color == BLUE ? "BLUE" : (clock_color == GREEN ? "GREEN" : "RED"));

        // Save new color choice to preferences
        preferences.begin("wifi_clock", false);
        preferences.putUChar("color", clock_color);
        preferences.end();

        switch (mode) {
          case MODE_TIME:
            printLocalTime();
            break;
         case MODE_TEMPERATURE:
            printTemperature();
            break;
          case MODE_BLANK:
            aip33628_clear_all();
            break;
          default:
            break;
        }
      }
    }
  }
  btn2_last_state = btn2_pressed ? LOW : HIGH;
}

// ==================================================
// Setup & Main Loop
// ==================================================

void setup()
{
  pinMode(BUZZER_PIN, OUTPUT);
  tone(BUZZER_PIN, 2000, 500);

  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);

  aip33628_init(AIP33628_1_CLK_PIN, AIP33628_1_DATA_PIN,
                AIP33628_2_CLK_PIN, AIP33628_2_DATA_PIN);
  aip33628_set_current(8);

  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
    delay(10);
  }
  Serial.println("\n--- ESP32-C3 Modular Dual Clock (NTP + DS1302) ---");

  // Check if either button is pressed at startup OR if force_config flag was set in Preferences
  bool btn1 = (digitalRead(BUTTON1_PIN) == LOW);
  bool btn2 = (digitalRead(BUTTON2_PIN) == LOW);

  preferences.begin("wifi_clock", false); // read-write
  bool forceConfig = preferences.getBool("force_config", false);
  if (forceConfig) {
    preferences.putBool("force_config", false); // clear flag for subsequent boots
  }
  preferences.end();

  if (btn1 || btn2 || forceConfig) {
    in_config_mode = true;
    display_conf();
    config_mode_init();
    return;
  }

  // Normal boot mode: load settings from Preferences flash
  if (!loadPreferences()) {
    Serial.println("[Boot] Preferences missing or invalid! Displaying BAD for 2s then rebooting into Config Mode...");
    display_bad();
    delay(2000);
    preferences.begin("wifi_clock", false);
    preferences.putBool("force_config", true);
    preferences.end();
    ESP.restart();
  }

  display_hi();
  delay(2000);

  ds1302_init(DS1302_CE_PIN, DS1302_CLK_PIN, DS1302_DAT_PIN);

  // Construct effective POSIX timezone string based on DST setting
  String effectiveTz = timeZone;
  if (!dst_enabled) {
    int commaIdx = effectiveTz.indexOf(',');
    if (commaIdx != -1) {
      effectiveTz = effectiveTz.substring(0, commaIdx);
    }
    int i = 0;
    while (i < (int)effectiveTz.length() && !isDigit(effectiveTz[i]) && effectiveTz[i] != '+' && effectiveTz[i] != '-') i++;
    while (i < (int)effectiveTz.length() && (isDigit(effectiveTz[i]) || effectiveTz[i] == '+' || effectiveTz[i] == '-' || effectiveTz[i] == ':')) i++;
    effectiveTz = effectiveTz.substring(0, i);
  }

  setenv("TZ", effectiveTz.c_str(), 1);
  tzset();

  bool rtcValid = syncSystemFromRtc();
  if (rtcValid) {
    Serial.print("Initial RTC Local Time: ");
    printLocalTime();
  } else {
    Serial.println("time invalid");
  }

  sntp_set_time_sync_notification_cb(timeSyncCallback);
  sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
  configTzTime(effectiveTz.c_str(), active_ntp_server1.c_str(), active_ntp_server2.c_str());

  time_t currentNow = time(NULL);
  next_minute_print_epoch = (currentNow / 60 + 1) * 60;
  printScheduledTime("[Schedule] Next minute print alarm:",
                     next_minute_print_epoch);

  // Start initial NTP sync non-blockingly; progress will be handled in loop()
  startNtpSync();

  Serial.println("\nSetup complete. Running wall-clock alarm loop:\n");
}

void loop()
{
  if (in_config_mode) {
    process_config_mode();
    delay(5);
    return;
  }

  time_t now = time(NULL);

  updateNtpTimeValidStatus(now);

  if (next_minute_print_epoch > 0 && now >= next_minute_print_epoch) {
    switch (mode) {
      case MODE_TIME:
        printLocalTime();
        break;
      case MODE_TEMPERATURE:
        printTemperature();
        break;
      case MODE_BLANK:
        aip33628_clear_all();
        break;
      default:
        break;
    }
    next_minute_print_epoch = (now / 60 + 1) * 60;
  }

  process_ntp_sync(now);

  process_brightness();
  process_temperature();
  process_buttons();

  delay(10);
}
