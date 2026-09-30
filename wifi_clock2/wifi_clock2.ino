#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <sys/time.h>
#include <esp_sntp.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <vector>
#include "ds1302.h"
#include "aip33628.h"

#define BLUE 0
#define GREEN 1
#define RED 2

// ESP32-C3 SuperMini Pin Definitions for HU-058 Board

#define PHOTO_RES_PIN       0 // ADC input from photoresistor

#define AIP33628_1_CLK_PIN  1 // Output to AIP33628
#define AIP33628_1_DATA_PIN 2 // Output to AIP33628
#define AIP33628_2_CLK_PIN  3 // Output to AIP33628
#define AIP33628_2_DATA_PIN 4 // Output to AIP33628

#define DS1302_CE_PIN  5  // Output to DS1302
#define DS1302_CLK_PIN 6  // Output to DS1302
#define DS1302_DAT_PIN 7  // Bi-directional!

#define BUTTON1_PIN        20 // Input to ESP32 (internal pullup)
#define BUTTON2_PIN        21 // Input to ESP32 (internal pullup)

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

// Preferences & Wi-Fi Settings
Preferences preferences;
String ssid = "";
String password = "";
String timeZone = "CST6CDT,M3.2.0,M11.1.0";
bool dst_enabled = true;
bool use_24h = false;
uint8_t clock_color = GREEN;
bool display_on = true;

// Web Server & Captive Portal objects for Config Mode
DNSServer dnsServer;
WebServer server(80);

String stored_ssid = "";
String stored_password = "";
String stored_timezone = "CST6CDT,M3.2.0,M11.1.0";
bool stored_use_24h = false;
bool stored_dst_enable = true;

// NTP Servers
bool use_default_ntp = true;
String active_ntp_server1 = "pool.ntp.org";
String active_ntp_server2 = "time.nist.gov";

bool stored_use_default_ntp = true;
String stored_ntp_server1 = "pool.ntp.org";
String stored_ntp_server2 = "time.nist.gov";

// Global State & Alarms
bool ntp_time_valid = false;
time_t last_successful_ntp_epoch = 0;

time_t next_sync_epoch = 0;
time_t next_minute_print_epoch = 0;

volatile bool sntpSyncTriggered = false;

// This table gives a name (index) to every RGB LED on the clock board.
const struct {
  uint8_t driver;
  uint8_t phase; // COM pair sequence index
  uint8_t segment;
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

// This table lists the LEDs needed to form hex characters 0 to F.
const uint8_t hex_digits[16][9] = {
  {0, 1, 2, 3, 4, 5, 255, 0},
  {1, 2, 255, 0, 0, 0, 0, 0},
  {0, 1, 6, 4, 3, 255, 0, 0},
  {0, 1, 2, 3, 6, 255, 0, 0}, // 3
  {5, 1, 6, 2, 255, 0, 0, 0},
  {0, 5, 6, 2, 3, 255, 0, 0},
  {0, 5, 4, 3, 2, 6, 255, 0},
  {0, 1, 2, 255, 0, 0, 0, 0}, // 7
  {0, 1, 2, 3, 4, 5, 6, 255},
  {6, 5, 0, 1, 2, 3, 255, 0},
  {0, 5, 1, 6, 4, 2, 255, 0}, // A (10)
  {5, 4, 3, 2, 6, 255, 0, 0}, // B (11)
  {0, 5, 4, 3, 255, 0, 0, 0}, // C (12)
  {1, 2, 3, 4, 6, 255, 0, 0}, // D (13)
  {0, 5, 4, 3, 6, 255, 0, 0}, // E (14)
  {0, 5, 6, 4, 255, 0, 0, 0}  // F (15)
};

// Display a digit 0-F on one of the 4 7-segment blocks.
void display_digit(uint8_t base, uint8_t digit, uint8_t color)
{
  int i = 0;
  uint8_t c;

  digit &= 0xf;
  while ((c = hex_digits[digit][i++]) != 255) {
    aip33628_ena_led(led_list[c + base].driver,
                     led_list[c + base].phase,
                     led_list[c + base].segment + color);
  }
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

// Displays "C0F6" on the LEDs in blue
void display_c0f6(void)
{
  aip33628_clear_all();
  display_digit(HOUR_10S, 12, BLUE); // C
  display_digit(HOUR_1S, 0, BLUE);   // 0
  display_digit(MIN_10S, 15, BLUE);  // F
  display_digit(MIN_1S, 6, BLUE);    // 6
}

// Load configuration from ESP32 Preferences
bool loadPreferences(void)
{
  preferences.begin("wifi_clock", true); // read-only
  stored_ssid = preferences.getString("ssid", "");
  stored_password = preferences.getString("password", "");
  stored_timezone = preferences.getString("timezone", "");
  stored_use_24h = preferences.getBool("use_24h", false);
  stored_dst_enable = preferences.getBool("dst", true);
  clock_color = preferences.getUChar("color", GREEN);
  stored_use_default_ntp = preferences.getBool("ntp_default", true);
  stored_ntp_server1 = preferences.getString("ntp_server1", "pool.ntp.org");
  stored_ntp_server2 = preferences.getString("ntp_server2", "time.nist.gov");
  preferences.end();

  if (stored_ssid.length() == 0 || stored_timezone.length() == 0) {
    return false;
  }

  ssid = stored_ssid;
  password = stored_password;
  timeZone = stored_timezone;
  use_24h = stored_use_24h;
  dst_enabled = stored_dst_enable;
  use_default_ntp = stored_use_default_ntp;

  if (use_default_ntp) {
    active_ntp_server1 = "pool.ntp.org";
    active_ntp_server2 = "time.nist.gov";
  } else {
    active_ntp_server1 = stored_ntp_server1;
    active_ntp_server2 = stored_ntp_server2;
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
  if (!getLocalTime(&timeinfo)) {
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
  getLocalTime(&timeinfo);

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

  if (!display_on) {
    aip33628_clear_all();
    return;
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
// Web Server & Captive Portal Handlers
// ===========================================================

void handleRoot()
{
  String html = R"rawliteral(<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Clock Configuration</title>
<style>
body { font-family: Arial, sans-serif; margin: 20px; background: #f0f2f5; color: #333; }
.card { max-width: 420px; margin: auto; background: white; padding: 24px; border-radius: 8px; box-shadow: 0 2px 8px rgba(0,0,0,0.1); }
h2 { margin-top: 0; color: #1a73e8; text-align: center; }
label { display: block; margin-top: 14px; font-weight: bold; }
input[type=text], select { width: 100%; padding: 8px; margin-top: 4px; box-sizing: border-box; border: 1px solid #ccc; border-radius: 4px; font-size: 14px; }
.row { display: flex; gap: 8px; margin-top: 4px; }
.btn { background: #1a73e8; color: white; border: none; padding: 10px 16px; border-radius: 4px; cursor: pointer; font-size: 14px; }
.btn:hover { background: #1557b0; }
.btn-cancel { background: #757575; }
.btn-cancel:hover { background: #5a5a5a; }
.btn-scan { background: #34a853; width: 100%; margin-top: 6px; }
.checkbox-container { display: flex; align-items: center; margin-top: 14px; }
.checkbox-container input { margin-right: 8px; width: 18px; height: 18px; }
.actions { margin-top: 24px; display: flex; justify-content: space-between; }
#ap-list { display: none; margin-top: 8px; border: 1px solid #ccc; max-height: 150px; overflow-y: auto; background: #fafafa; border-radius: 4px; }
.ap-item { padding: 8px; cursor: pointer; border-bottom: 1px solid #eee; }
.ap-item:hover { background: #e8f0fe; }
</style>
</head>
<body>
<div class="card">
<h2>Clock Setup</h2>
<form action="/save" method="POST">
<label for="ssid">Wi-Fi SSID:</label>
<input type="text" id="ssid" name="ssid" required value="%SAVED_SSID%">
<button type="button" class="btn btn-scan" onclick="scanWifi()">Choose from list</button>
<div id="ap-list"></div>

<label for="password">Wi-Fi Password:</label>
<input type="text" id="password" name="password" placeholder="Enter new password (leave blank to keep current)" %PWD_DISABLED%>
<div class="checkbox-container" style="margin-top:6px;">
  <input type="checkbox" id="open_net" name="open_net" value="1" %OPEN_NET_CHECKED% onchange="togglePasswordInput(this)">
  <label for="open_net" style="margin-top:0;font-weight:normal">No password (open)</label>
</div>

<label for="timezone">Timezone:</label>
<select id="timezone" name="timezone">
  <optgroup label="North America">
    <option value="HST10">Hawaii (UTC-10)</option>
    <option value="AKST9AKDT,M3.2.0,M11.1.0">Alaska (UTC-9)</option>
    <option value="PST8PDT,M3.2.0,M11.1.0">Pacific Time - US & Canada (UTC-8)</option>
    <option value="MST7">Mountain Time - Arizona (UTC-7, No DST)</option>
    <option value="MST7MDT,M3.2.0,M11.1.0">Mountain Time - US & Canada (UTC-7)</option>
    <option value="CST6CDT,M3.2.0,M11.1.0">Central Time - US & Canada (UTC-6)</option>
    <option value="EST5EDT,M3.2.0,M11.1.0">Eastern Time - US & Canada (UTC-5)</option>
    <option value="AST4ADT,M3.2.0,M11.1.0">Atlantic Time - Canada (UTC-4)</option>
    <option value="NST3:30NDT,M3.2.0,M11.1.0">Newfoundland (UTC-3:30)</option>
  </optgroup>
  <optgroup label="Central & South America">
    <option value="CST6">Central America / Mexico City (UTC-6)</option>
    <option value="EST5">Bogota, Lima, Quito (UTC-5)</option>
    <option value="CLT4CLST,M9.1.0/24,M4.1.0/24">Santiago, Chile (UTC-4)</option>
    <option value="BRT3">Buenos Aires, Brasilia (UTC-3)</option>
  </optgroup>
  <optgroup label="Europe & UTC">
    <option value="UTC0">UTC / GMT (UTC+0)</option>
    <option value="GMT0BST,M3.5.0/1,M10.5.0/2">London, Dublin, Lisbon (UTC+0)</option>
    <option value="CET-1CEST,M3.5.0,M10.5.0/3">Central Europe - Paris, Berlin, Rome, Madrid (UTC+1)</option>
    <option value="EET-2EEST,M3.5.0/3,M10.5.0/4">Eastern Europe - Athens, Helsinki, Kyiv, Cairo (UTC+2)</option>
    <option value="MSK-3">Moscow, Istanbul, Riyadh, Nairobi (UTC+3)</option>
  </optgroup>
  <optgroup label="Africa & Middle East">
    <option value="SAST-2">Johannesburg, Harare (UTC+2)</option>
    <option value="AST-3">Riyadh, Kuwait, Baghdad (UTC+3)</option>
    <option value="IRST-3:30">Tehran (UTC+3:30)</option>
    <option value="GST-4">Dubai, Abu Dhabi, Muscat (UTC+4)</option>
  </optgroup>
  <optgroup label="Asia">
    <option value="PKT-5">Karachi, Tashkent (UTC+5)</option>
    <option value="IST-5:30">India - New Delhi, Mumbai, Kolkata (UTC+5:30)</option>
    <option value="NPT-5:45">Nepal - Kathmandu (UTC+5:45)</option>
    <option value="BST-6">Dhaka, Almaty (UTC+6)</option>
    <option value="MMT-6:30">Yangon, Myanmar (UTC+6:30)</option>
    <option value="ICT-7">Bangkok, Hanoi, Jakarta (UTC+7)</option>
    <option value="CST-8">China, Singapore, Hong Kong, Taipei, Perth (UTC+8)</option>
    <option value="JST-9">Tokyo, Seoul (UTC+9)</option>
  </optgroup>
  <optgroup label="Australia & Pacific">
    <option value="AWST-8">Perth - Western Australia (UTC+8)</option>
    <option value="ACST-9:30ACDT,M10.1.0,M4.1.0/3">Adelaide, Darwin (UTC+9:30)</option>
    <option value="AEST-10">Brisbane (UTC+10, No DST)</option>
    <option value="AEST-10AEDT,M10.1.0,M4.1.0/3">Sydney, Melbourne, Canberra (UTC+10)</option>
    <option value="NZST-12NZDT,M9.5.0,M4.1.0/3">Auckland, Wellington (UTC+12)</option>
  </optgroup>
</select>

<div class="checkbox-container">
  <input type="checkbox" id="dst" name="dst" value="1" %DST_CHECKED%>
  <label for="dst" style="margin-top:0;font-weight:normal">Automatically adjust for Daylight Saving Time</label>
</div>

<label for="time_format">Time Format:</label>
<select id="time_format" name="time_format">
  <option value="12">12-hour (with AM indicator)</option>
  <option value="24">24-hour</option>
</select>

<div class="checkbox-container">
  <input type="checkbox" id="ntp_default" name="ntp_default" value="1" %NTP_DEFAULT_CHECKED% onchange="toggleNtpInputs(this)">
  <label for="ntp_default" style="margin-top:0;font-weight:normal">Use default NTP servers</label>
</div>

<label for="ntp_server1">Primary NTP Server:</label>
<input type="text" id="ntp_server1" name="ntp_server1" value="%NTP_SERVER1%" %NTP_DISABLED%>

<label for="ntp_server2">Secondary NTP Server:</label>
<input type="text" id="ntp_server2" name="ntp_server2" value="%NTP_SERVER2%" %NTP_DISABLED%>

<label for="color">LED Display Color:</label>
<select id="color" name="color">
  <option value="1" %COLOR_GREEN%>Green</option>
  <option value="2" %COLOR_RED%>Red</option>
  <option value="0" %COLOR_BLUE%>Blue</option>
</select>

<div class="actions">
  <button type="button" class="btn btn-cancel" onclick="window.location.href='/cancel'">Cancel</button>
  <button type="submit" class="btn">Save & Reboot</button>
</div>
</form>
</div>
<script>
function togglePasswordInput(cb) {
  var pwdInput = document.getElementById('password');
  if (cb.checked) {
    pwdInput.value = '';
    pwdInput.disabled = true;
  } else {
    pwdInput.disabled = false;
  }
}
function toggleNtpInputs(cb) {
  var s1 = document.getElementById('ntp_server1');
  var s2 = document.getElementById('ntp_server2');
  if (cb.checked) {
    s1.value = 'pool.ntp.org';
    s2.value = 'time.nist.gov';
    s1.disabled = true;
    s2.disabled = true;
  } else {
    s1.disabled = false;
    s2.disabled = false;
  }
}
window.addEventListener('DOMContentLoaded', function() {
  var tzSelect = document.getElementById('timezone');
  if (tzSelect) {
    tzSelect.value = "%SAVED_TZ%";
  }
  var tfSelect = document.getElementById('time_format');
  if (tfSelect) {
    tfSelect.value = "%SAVED_TF%";
  }
  var cb = document.getElementById('open_net');
  if (cb) togglePasswordInput(cb);
  var ntpCb = document.getElementById('ntp_default');
  if (ntpCb) toggleNtpInputs(ntpCb);
});
function scanWifi() {
  var list = document.getElementById('ap-list');
  list.style.display = 'block';
  list.innerHTML = '<div class="ap-item">Scanning...</div>';
  fetch('/scan').then(r => r.json()).then(ssids => {
    if (ssids.length === 0) {
      list.innerHTML = '<div class="ap-item">No networks found</div>';
      return;
    }
    list.innerHTML = '';
    ssids.forEach(s => {
      var d = document.createElement('div');
      d.className = 'ap-item';
      d.innerText = s;
      d.onclick = function() {
        document.getElementById('ssid').value = s;
        list.style.display = 'none';
      };
      list.appendChild(d);
    });
  }).catch(e => {
    list.innerHTML = '<div class="ap-item">Scan failed</div>';
  });
}
</script>
</body>
</html>)rawliteral";

  bool isCurrentlyOpen = (stored_password.length() == 0 && stored_ssid.length() > 0);
  html.replace("%OPEN_NET_CHECKED%", (isCurrentlyOpen ? "checked" : ""));
  html.replace("%PWD_DISABLED%", (isCurrentlyOpen ? "disabled" : ""));

  html.replace("%SAVED_SSID%", stored_ssid);
  html.replace("%SAVED_TZ%", stored_timezone);
  html.replace("%SAVED_TF%", stored_use_24h ? "24" : "12");
  html.replace("%DST_CHECKED%", (stored_dst_enable ? "checked" : ""));
  html.replace("%NTP_DEFAULT_CHECKED%", (stored_use_default_ntp ? "checked" : ""));
  html.replace("%NTP_SERVER1%", stored_ntp_server1);
  html.replace("%NTP_SERVER2%", stored_ntp_server2);
  html.replace("%NTP_DISABLED%", (stored_use_default_ntp ? "disabled" : ""));

  html.replace("%COLOR_GREEN%", (clock_color == GREEN ? "selected" : ""));
  html.replace("%COLOR_RED%", (clock_color == RED ? "selected" : ""));
  html.replace("%COLOR_BLUE%", (clock_color == BLUE ? "selected" : ""));

  server.send(200, "text/html", html);
}

void handleScan()
{
  int n = WiFi.scanNetworks();
  String json = "[";
  if (n > 0) {
    std::vector<String> uniqueSsids;
    for (int i = 0; i < n; ++i) {
      String s = WiFi.SSID(i);
      if (s.length() > 0) {
        bool dup = false;
        for (const auto& existing : uniqueSsids) {
          if (existing == s) { dup = true; break; }
        }
        if (!dup) uniqueSsids.push_back(s);
      }
    }
    for (size_t i = 0; i < uniqueSsids.size(); ++i) {
      json += "\"" + uniqueSsids[i] + "\"";
      if (i + 1 < uniqueSsids.size()) json += ",";
    }
  }
  json += "]";
  WiFi.scanDelete();
  server.send(200, "application/json", json);
}

void handleSave()
{
  if (server.hasArg("ssid")) {
    String newSsid = server.arg("ssid");
    String newPassword = server.arg("password");
    String newTz = server.arg("timezone");
    bool new24h = (server.arg("time_format") == "24");
    bool newDst = server.hasArg("dst");
    bool isOpenNet = server.hasArg("open_net");
    uint8_t newColor = (uint8_t)server.arg("color").toInt();

    bool newNtpDefault = server.hasArg("ntp_default");
    String newNtp1 = server.arg("ntp_server1");
    String newNtp2 = server.arg("ntp_server2");
    if (newNtpDefault || newNtp1.length() == 0) {
      newNtp1 = "pool.ntp.org";
    }
    if (newNtpDefault || newNtp2.length() == 0) {
      newNtp2 = "time.nist.gov";
    }

    preferences.begin("wifi_clock", false); // read-write
    preferences.putString("ssid", newSsid);

    if (isOpenNet) {
      preferences.putString("password", "");
    } else if (newPassword.length() > 0) {
      preferences.putString("password", newPassword);
    }

    preferences.putString("timezone", newTz);
    preferences.putBool("use_24h", new24h);
    preferences.putBool("dst", newDst);
    preferences.putUChar("color", newColor);
    preferences.putBool("ntp_default", newNtpDefault);
    preferences.putString("ntp_server1", newNtp1);
    preferences.putString("ntp_server2", newNtp2);
    preferences.end();

    Serial.println("[Config] Preferences saved successfully. Rebooting...");
    String html = "<html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"></head><body style=\"font-family:sans-serif;text-align:center;margin-top:50px;\"><h2>Settings saved and clock rebooted!</h2><p>You must now rejoin your normal WiFi network</p></body></html>";
    server.send(200, "text/html", html);
    delay(1000);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleCancel()
{
  Serial.println("[Config] Configuration cancelled. Rebooting...");
  String html = "<html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"></head><body style=\"font-family:sans-serif;text-align:center;margin-top:50px;\"><h2>Cancelled</h2><p>Rebooting clock...</p></body></html>";
  server.send(200, "text/html", html);
  delay(1000);
  ESP.restart();
}

void handleNotFound()
{
  server.sendHeader("Location", "http://192.168.199.1/", true);
  server.send(302, "text/plain", "");
}

void runConfigMode()
{
  Serial.println("[Config] Entering Configuration Mode (AP: clock0, IP: 192.168.199.1)...");

  preferences.begin("wifi_clock", true);
  stored_ssid = preferences.getString("ssid", "");
  stored_password = preferences.getString("password", "");
  stored_timezone = preferences.getString("timezone", "CST6CDT,M3.2.0,M11.1.0");
  stored_use_24h = preferences.getBool("use_24h", false);
  stored_dst_enable = preferences.getBool("dst", true);
  clock_color = preferences.getUChar("color", GREEN);
  stored_use_default_ntp = preferences.getBool("ntp_default", true);
  stored_ntp_server1 = preferences.getString("ntp_server1", "pool.ntp.org");
  stored_ntp_server2 = preferences.getString("ntp_server2", "time.nist.gov");
  preferences.end();

  display_c0f6();

  WiFi.mode(WIFI_AP_STA);
  IPAddress local_ip(192, 168, 199, 1);
  IPAddress gateway(192, 168, 199, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_ip, gateway, subnet);
  WiFi.softAP("clock0");

  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", local_ip);

  if (MDNS.begin("clock")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("[Config] mDNS responder started for 'clock.local'");
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/cancel", HTTP_GET, handleCancel);
  server.on("/cancel", HTTP_POST, handleCancel);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("[Config] Web server started.");

  while (true) {
    dnsServer.processNextRequest();
    server.handleClient();
    delay(5);
  }
}

// ===========================================================
// System Time & Sync Logic
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

bool performNtpSync()
{
  Serial.println("\n[Radio] Powering on Wi-Fi for NTP sync...");
  sntpSyncTriggered = false;

  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.setHostname("clock0");
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

  WiFi.begin(ssid.c_str(), password.c_str());

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startAttempt < 18000)) {
    delay(250);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n[Radio] Wi-Fi connection failed. Keeping current system time.");
    stopWiFi();
    updateNtpTimeValidStatus(time(NULL));
    return false;
  }
  Serial.printf("\n[Radio] Connected (RSSI: %d dBm).\n", WiFi.RSSI());

  delay(500);

  sntp_restart();

  startAttempt = millis();
  while (!sntpSyncTriggered && (millis() - startAttempt < 15000)) {
    delay(100);
  }

  stopWiFi();

  time_t now = time(NULL);

  if (sntpSyncTriggered) {
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
    return true;
  } else {
    Serial.println("[NTP] Sync timed out.");
    updateNtpTimeValidStatus(now);
    return false;
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
    if (v > 2900)
       aip33628_set_current(0); // Room is dark
    else if (v < 2300)
       aip33628_set_current(8);
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
      display_c0f6();
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
        // Brief press of Button 1 -> toggle display ON / OFF
        display_on = !display_on;
        Serial.printf("[Button 1] Brief press detected. Display toggled %s.\n", display_on ? "ON" : "OFF");
        if (!display_on) {
          aip33628_clear_all();
        } else {
          printLocalTime();
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
      display_c0f6();
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

        if (display_on) {
          printLocalTime();
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
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
    delay(10);
  }
  Serial.println("\n--- ESP32-C3 Modular Dual Clock (NTP + DS1302) ---");

  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);

  aip33628_init(AIP33628_1_CLK_PIN, AIP33628_1_DATA_PIN,
                AIP33628_2_CLK_PIN, AIP33628_2_DATA_PIN);
  aip33628_set_current(8);

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
    runConfigMode(); // Enters Config Mode loop (AP: clock0, HTTP server)
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

  bool syncOk = performNtpSync();

  time_t currentNow = time(NULL);

  if (syncOk) {
    Serial.print("[Boot] Synchronized Local Time: ");
    printLocalTime();
  }

  next_sync_epoch = calculateNextSyncEpoch(currentNow, syncOk);
  printScheduledTime("[Schedule] Next NTP sync alarm:", next_sync_epoch);

  next_minute_print_epoch = (currentNow / 60 + 1) * 60;
  printScheduledTime("[Schedule] Next minute print alarm:",
                     next_minute_print_epoch);

  Serial.println("\nSetup complete. Running wall-clock alarm loop:\n");
}

void loop()
{
  time_t now = time(NULL);

  updateNtpTimeValidStatus(now);

  if (next_minute_print_epoch > 0 && now >= next_minute_print_epoch) {
    printLocalTime();
    next_minute_print_epoch = (now / 60 + 1) * 60;
  }

  if (next_sync_epoch > 0 && now >= next_sync_epoch) {
    bool syncOk = performNtpSync();
    now = time(NULL);
    next_sync_epoch = calculateNextSyncEpoch(now, syncOk);
    printScheduledTime("[Schedule] Next NTP sync alarm:", next_sync_epoch);
  }

  process_brightness();
  process_buttons();

  delay(100);
}
