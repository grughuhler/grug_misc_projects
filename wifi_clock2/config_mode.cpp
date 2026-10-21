#include "config_mode.h"
#include <WiFi.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <vector>

// Web Server & Captive Portal objects for Config Mode
static Preferences config_preferences;
static DNSServer dnsServer;
static WebServer server(80);

static String stored_ssid = "";
static String stored_password = "";
static String stored_timezone = "CST6CDT,M3.2.0,M11.1.0";
static bool stored_use_24h = false;
static bool stored_dst_enable = true;
static uint8_t stored_color = GREEN;
static bool stored_use_default_ntp = true;
static String stored_ntp_server1 = "pool.ntp.org";
static String stored_ntp_server2 = "time.nist.gov";

static void handleRoot()
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

  html.replace("%COLOR_GREEN%", (stored_color == GREEN ? "selected" : ""));
  html.replace("%COLOR_RED%", (stored_color == RED ? "selected" : ""));
  html.replace("%COLOR_BLUE%", (stored_color == BLUE ? "selected" : ""));

  server.send(200, "text/html", html);
}

static void handleScan()
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

static void handleSave()
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

    config_preferences.begin("wifi_clock", false); // read-write
    config_preferences.putString("ssid", newSsid);

    if (isOpenNet) {
      config_preferences.putString("password", "");
    } else if (newPassword.length() > 0) {
      config_preferences.putString("password", newPassword);
    }

    config_preferences.putString("timezone", newTz);
    config_preferences.putBool("use_24h", new24h);
    config_preferences.putBool("dst", newDst);
    config_preferences.putUChar("color", newColor);
    config_preferences.putBool("ntp_default", newNtpDefault);
    config_preferences.putString("ntp_server1", newNtp1);
    config_preferences.putString("ntp_server2", newNtp2);
    config_preferences.end();

    Serial.println("[Config] Preferences saved successfully. Rebooting...");
    String html = "<html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"></head><body style=\"font-family:sans-serif;text-align:center;margin-top:50px;\"><h2>Settings saved and clock rebooted!</h2><p>You must now rejoin your normal WiFi network</p></body></html>";
    server.send(200, "text/html", html);
    delay(1000);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

static void handleCancel()
{
  Serial.println("[Config] Configuration cancelled. Rebooting...");
  String html = "<html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"></head><body style=\"font-family:sans-serif;text-align:center;margin-top:50px;\"><h2>Cancelled</h2><p>Rebooting clock...</p></body></html>";
  server.send(200, "text/html", html);
  delay(1000);
  ESP.restart();
}

static void handleNotFound()
{
  server.sendHeader("Location", "http://192.168.199.1/", true);
  server.send(302, "text/plain", "");
}

void config_mode_init(void)
{
  Serial.println("[Config] Entering Configuration Mode (AP: clock1, IP: 192.168.199.1)...");

  config_preferences.begin("wifi_clock", true);
  stored_ssid = config_preferences.getString("ssid", "");
  stored_password = config_preferences.getString("password", "");
  stored_timezone = config_preferences.getString("timezone", "CST6CDT,M3.2.0,M11.1.0");
  stored_use_24h = config_preferences.getBool("use_24h", false);
  stored_dst_enable = config_preferences.getBool("dst", true);
  stored_color = config_preferences.getUChar("color", GREEN);
  stored_use_default_ntp = config_preferences.getBool("ntp_default", true);
  stored_ntp_server1 = config_preferences.getString("ntp_server1", "pool.ntp.org");
  stored_ntp_server2 = config_preferences.getString("ntp_server2", "time.nist.gov");
  config_preferences.end();

  WiFi.mode(WIFI_AP_STA);
  IPAddress local_ip(192, 168, 199, 1);
  IPAddress gateway(192, 168, 199, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_ip, gateway, subnet);
  WiFi.softAP("clock1");

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
}

void process_config_mode(void)
{
  dnsServer.processNextRequest();
  server.handleClient();
}
