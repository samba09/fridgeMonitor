// Kühlschrank-Monitor
//
// ESP32 + 2x DS18B20 + TM1637 4-digit display.
// - Own WiFi hotspot "Kuehlschrank" with web page at http://192.168.4.1 (for setup,
//   optionally off while the home WiFi works)
// - Optional: joins the home WiFi (configured on the web page)
// - Display alternates between the two probes (whole degrees; 0.0 to 9.9 °C with one
//   decimal, "_" as decimal separator because the display has no decimal points)
// - History: 1 value per minute and probe, 24 h, kept in RAM
// - Firmware update via web page
//
// Wiring (see partlist.md):
//   DS18B20 adapter: VCC -> GPIO16 (sensor power), GND -> GND, DAT -> GPIO4
//   TM1637 display:  VCC -> 3V3, GND -> GND, DIO -> GPIO23, CLK -> GPIO18

#include <Arduino.h>
#include <DNSServer.h>
#include <DallasTemperature.h>
#include <ESPmDNS.h>
#include <OneWire.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <sys/time.h>
#include <time.h>

#include "tm1637.h"
#include "webpage.h"

static const char *FW_VERSION = "1.2.0";

// ---------------------------------------------------------------- config
constexpr uint8_t PIN_ONEWIRE = 4;
constexpr uint8_t PIN_SENSOR_PWR = 16;
constexpr uint8_t PIN_TM_CLK = 18;
constexpr uint8_t PIN_TM_DIO = 23;
constexpr uint8_t PIN_BOOT = 0;

constexpr int NSLOTS = 2;
constexpr int HIST_LEN = 24 * 60;  // 1 value per minute, 24 h
constexpr int16_t NODATA = INT16_MIN;

constexpr uint32_t SAMPLE_INTERVAL_MS = 60UL * 1000;
constexpr uint32_t READ_INTERVAL_MS = 2000;
constexpr uint32_t CONVERSION_MS = 800;
constexpr uint32_t SENSOR_STALE_MS = 30UL * 1000;
constexpr uint32_t RESCAN_INTERVAL_MS = 30UL * 1000;
constexpr uint32_t DISPLAY_FRAME_MS = 3000;
constexpr uint32_t STA_RETRY_MS = 60UL * 1000;
constexpr uint32_t AP_OFF_DELAY_MS = 60UL * 1000;        // home WiFi stable this long -> hotspot may go off
constexpr uint32_t AP_FORCE_OFF_MS = 10UL * 60 * 1000;   // ... even if a phone is still connected
constexpr uint32_t AP_FALLBACK_MS = 2UL * 60 * 1000;     // home WiFi gone this long -> hotspot back on
constexpr uint32_t RESET_HOLD_MS = 10UL * 1000;          // hold BOOT this long for factory reset
constexpr uint32_t PROBE_STUCK_MS = 3UL * 60 * 1000;     // probe found but no valid reading -> power-cycle
constexpr uint32_t PROBE_CYCLE_MIN_MS = 5UL * 60 * 1000; // at most one power-cycle per 5 min
constexpr int WDT_TIMEOUT_S = 30;                        // restart if loop() hangs this long

static const char *AP_SSID = "Kuehlschrank";
static const char *AP_PASS_DEFAULT = "kuehlschrank";  // min. 8 characters
static const char *HOSTNAME = "kuehlschrank";
static const char *TZ_INFO = "CET-1CEST,M3.5.0,M10.5.0/3";

// ---------------------------------------------------------------- globals
OneWire oneWire(PIN_ONEWIRE);
DallasTemperature dallas(&oneWire);
TM1637 display(PIN_TM_CLK, PIN_TM_DIO);
WebServer server(80);
DNSServer dns;
Preferences prefs;

struct Slot {
  bool configured = false;  // a probe address is assigned to this slot
  bool present = false;     // probe found on the bus
  uint8_t addr[8] = {0};
  String name;
  float temp = NAN;         // last valid reading, NAN if none/stale
  uint32_t lastOkMs = 0;
  double sum = 0;           // accumulation for the current minute
  uint16_t cnt = 0;
};

Slot slots[NSLOTS];
int16_t hist[NSLOTS][HIST_LEN];  // 0.1 °C, NODATA = no value
uint16_t histHead = 0;           // next write position
uint16_t histCount = 0;
uint32_t lastSampleMs = 0;

uint8_t brightness = 4;
String staSsid, staPass;
String apPass;
bool apAuto = true;      // hotspot only for setup: off while the home WiFi works
bool apActive = false;
bool staWasConnected = false;
uint32_t staLastTryMs = 0;
uint32_t staChangeMs = 0;  // last change of the home WiFi connection state
uint32_t apOnSinceMs = 0;  // hotspot switched on, or its settings changed
bool buttonOverride = false;  // display is showing the factory reset countdown

bool conversionPending = false;
uint32_t conversionStartMs = 0;
uint32_t lastReadMs = 0;
uint32_t lastRescanMs = 0;
uint8_t emptyScans = 0;
uint32_t lastPowerCycleMs = 0;
bool uploadRejected = false;

// display
uint8_t frameIndex = 0;
uint32_t frameStartMs = 0;
uint8_t ipFrames[5][4];
int8_t ipFrameCount = 0;  // > 0: IP address sequence is being shown
int8_t ipFramePos = 0;

// ---------------------------------------------------------------- helpers
bool timeValid() { return time(nullptr) > 1700000000; }

String addrToString(const uint8_t *a) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%02X-%02X%02X%02X%02X%02X%02X", a[0], a[6], a[5], a[4], a[3], a[2], a[1]);
  return String(buf);
}

String jsonEscape(const String &s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if ((uint8_t)c < 0x20) {
      char buf[8];
      snprintf(buf, sizeof(buf), "\\u%04x", c);
      out += buf;
    } else {
      out += c;
    }
  }
  return out;
}

// Trim and limit to 48 bytes (24 characters in the form, umlauts take 2 bytes)
// without cutting a UTF-8 character in half.
String cleanName(String s) {
  s.trim();
  if (s.length() > 48) {
    size_t n = 48;
    while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) n--;
    s = s.substring(0, n);
  }
  return s;
}

// ---------------------------------------------------------------- settings
void saveSlot(int i) {
  char key[4];
  snprintf(key, sizeof(key), "c%d", i);
  prefs.putBool(key, slots[i].configured);
  snprintf(key, sizeof(key), "a%d", i);
  prefs.putBytes(key, slots[i].addr, 8);
  snprintf(key, sizeof(key), "n%d", i);
  prefs.putString(key, slots[i].name);
}

void loadSettings() {
  prefs.begin("fm", false);
  brightness = prefs.getUChar("bright", 4);
  staSsid = prefs.getString("ssid", "");
  staPass = prefs.getString("pass", "");
  apPass = prefs.getString("appass", AP_PASS_DEFAULT);
  if (apPass.length() < 8) apPass = AP_PASS_DEFAULT;
  apAuto = prefs.getBool("apauto", true);
#ifdef WOKWI_SIM
  staSsid = "Wokwi-GUEST";  // simulated network of the Wokwi simulator, no password
  staPass = "";
#endif
  for (int i = 0; i < NSLOTS; i++) {
    char key[4];
    snprintf(key, sizeof(key), "c%d", i);
    slots[i].configured = prefs.getBool(key, false);
    snprintf(key, sizeof(key), "a%d", i);
    if (prefs.getBytes(key, slots[i].addr, 8) != 8) slots[i].configured = false;
    snprintf(key, sizeof(key), "n%d", i);
    slots[i].name = prefs.getString(key, "");
  }
}

// ---------------------------------------------------------------- history
void clearHistorySlot(int i) {
  for (int k = 0; k < HIST_LEN; k++) hist[i][k] = NODATA;
}

void pushHistory() {
  for (int i = 0; i < NSLOTS; i++) {
    int16_t v = NODATA;
    if (slots[i].cnt > 0) v = (int16_t)lround(slots[i].sum / slots[i].cnt * 10.0);
    hist[i][histHead] = v;
    slots[i].sum = 0;
    slots[i].cnt = 0;
  }
  histHead = (histHead + 1) % HIST_LEN;
  if (histCount < HIST_LEN) histCount++;
}

// ---------------------------------------------------------------- sensors
void sensorPowerCycle() {
  digitalWrite(PIN_SENSOR_PWR, LOW);
  delay(500);
  digitalWrite(PIN_SENSOR_PWR, HIGH);
  delay(50);
  lastPowerCycleMs = millis();
}

void scanBus() {
  uint8_t found[4][8];
  int n = 0;
  uint8_t a[8];
  oneWire.reset_search();
  while (n < 4 && oneWire.search(a)) {
    if (OneWire::crc8(a, 7) != a[7]) continue;
    if (a[0] != 0x28 && a[0] != 0x10 && a[0] != 0x22) continue;  // DS18B20/DS18S20/DS1822
    memcpy(found[n++], a, 8);
  }

  bool used[4] = {false, false, false, false};
  for (int i = 0; i < NSLOTS; i++) {
    slots[i].present = false;
    if (!slots[i].configured) continue;
    for (int j = 0; j < n; j++) {
      if (!used[j] && memcmp(found[j], slots[i].addr, 8) == 0) {
        used[j] = true;
        slots[i].present = true;
      }
    }
  }

  // assign new probes: first to empty slots, then to slots whose probe is missing
  for (int j = 0; j < n; j++) {
    if (used[j]) continue;
    int target = -1;
    for (int i = 0; i < NSLOTS && target < 0; i++)
      if (!slots[i].configured) target = i;
    for (int i = 0; i < NSLOTS && target < 0; i++)
      if (!slots[i].present) target = i;
    if (target < 0) break;
    Slot &s = slots[target];
    s.configured = true;
    s.present = true;
    memcpy(s.addr, found[j], 8);
    s.name = "";  // gets a default name after the first reading
    s.temp = NAN;
    clearHistorySlot(target);
    used[j] = true;
    saveSlot(target);
    Serial.printf("Neuer Fuehler %s -> Platz %d\n", addrToString(found[j]).c_str(), target + 1);
  }

  for (int i = 0; i < NSLOTS; i++)
    if (slots[i].present) dallas.setResolution(slots[i].addr, 12);

  emptyScans = (n == 0) ? emptyScans + 1 : 0;
  if (emptyScans >= 3) {  // bus may be stuck: power-cycle the probes
    Serial.println("Keine Fuehler gefunden, Fuehler-Strom aus/an");
    sensorPowerCycle();
    emptyScans = 0;
  }
  lastRescanMs = millis();
}

void assignDefaultName(int i) {
  Slot &s = slots[i];
  if (s.name.length() > 0 || isnan(s.temp)) return;
  String base = s.temp < -5.0f ? "Gefrierfach" : "Kühlschrank";
  String name = base;
  for (int k = 0; k < NSLOTS; k++)
    if (k != i && slots[k].name == name) name = base + " 2";
  s.name = name;
  saveSlot(i);
}

void handleSensors() {
  uint32_t now = millis();

  if (conversionPending && now - conversionStartMs >= CONVERSION_MS) {
    conversionPending = false;
    for (int i = 0; i < NSLOTS; i++) {
      Slot &s = slots[i];
      if (!s.present) continue;
      float t = dallas.getTempC(s.addr);
      bool ok = t > -100.0f && t < 125.0f && t != 85.0f;  // 85.0 = power-on value
      if (ok) {
        s.temp = t;
        s.lastOkMs = now;
        s.sum += t;
        s.cnt++;
        assignDefaultName(i);
      }
    }
  }

  bool needRescan = false;
  for (int i = 0; i < NSLOTS; i++) {
    Slot &s = slots[i];
    if (!isnan(s.temp) && now - s.lastOkMs > SENSOR_STALE_MS) s.temp = NAN;
    if (!s.configured || !s.present || isnan(s.temp)) needRescan = true;
  }
  if (needRescan && !conversionPending && now - lastRescanMs > RESCAN_INTERVAL_MS) scanBus();

  if (!conversionPending && now - lastReadMs >= READ_INTERVAL_MS) {
    lastReadMs = now;
    dallas.requestTemperatures();
    conversionPending = true;
    conversionStartMs = now;
  }

  // A probe that answers on the bus but delivers no valid readings (e.g. stuck at 85 °C
  // after a brown-out): switch the probe power off and on.
  if (!conversionPending && now - lastPowerCycleMs > PROBE_CYCLE_MIN_MS) {
    for (int i = 0; i < NSLOTS; i++) {
      const Slot &s = slots[i];
      uint32_t since = now - (s.lastOkMs > lastPowerCycleMs ? s.lastOkMs : lastPowerCycleMs);
      if (s.present && since > PROBE_STUCK_MS) {
        Serial.printf("Fuehler %d liefert keine Werte, Fuehler-Strom aus/an\n", i + 1);
        sensorPowerCycle();
        break;
      }
    }
  }

  // One history value per minute; minutes missed during a stall are stored as gaps
  // so that the time axis stays correct.
  int pushes = 0;
  while (now - lastSampleMs >= SAMPLE_INTERVAL_MS && pushes < HIST_LEN) {
    lastSampleMs += SAMPLE_INTERVAL_MS;
    pushHistory();  // the first push uses this minute's readings, further ones are gaps
    pushes++;
  }
  if (now - lastSampleMs >= SAMPLE_INTERVAL_MS) lastSampleMs = now;
}

// ---------------------------------------------------------------- display
// Whole degrees ("  4°" style, "-18°"), except 0.0 to 9.9 °C: one decimal with "_" as
// decimal separator ("4_5°"), since the display has a colon but no decimal points.
void formatTemp(float t, uint8_t out[4]) {
  for (int k = 0; k < 4; k++) out[k] = seg::BLANK;
  if (isnan(t)) {
    for (int k = 0; k < 4; k++) out[k] = seg::MINUS;
    return;
  }
  long tenths = lroundf(t * 10.0f);
  if (tenths >= 0 && tenths <= 99) {
    out[0] = seg::DIGITS[tenths / 10];
    out[1] = seg::UNDERSCORE;
    out[2] = seg::DIGITS[tenths % 10];
    out[3] = seg::DEGREE;
    return;
  }
  long v = lroundf(t);
  if (v > 99) {
    out[1] = seg::H;
    out[2] = seg::I;
    return;
  }
  if (v < -99) {
    out[1] = seg::L;
    out[2] = seg::O;
    return;
  }
  bool neg = v < 0;
  long a = labs(v);
  out[3] = seg::DEGREE;
  out[2] = seg::DIGITS[a % 10];
  if (a >= 10) {
    out[1] = seg::DIGITS[a / 10];
    if (neg) out[0] = seg::MINUS;
  } else if (neg) {
    out[1] = seg::MINUS;
  }
}

void formatNumberRight(int n, uint8_t out[4]) {
  for (int k = 0; k < 4; k++) out[k] = seg::BLANK;
  for (int pos = 3; pos >= 0; pos--) {
    out[pos] = seg::DIGITS[n % 10];
    n /= 10;
    if (n == 0) break;
  }
}

void startIpDisplay() {
  bool sta = WiFi.status() == WL_CONNECTED;
  if (!sta && !apActive) {  // home WiFi lost, hotspot not back yet: no address
    const uint8_t noIp[4] = {seg::I, seg::P, seg::MINUS, seg::MINUS};
    memcpy(ipFrames[0], noIp, 4);
    ipFrameCount = 1;
    ipFramePos = 0;
    frameStartMs = millis();
    display.show(ipFrames[0]);
    return;
  }
  IPAddress ip = sta ? WiFi.localIP() : WiFi.softAPIP();
  uint8_t head[4] = {seg::I, seg::P, seg::BLANK, seg::BLANK};
  if (!sta) {  // "AP" = own hotspot address
    head[0] = seg::A;
    head[1] = seg::P;
  }
  memcpy(ipFrames[0], head, 4);
  for (int k = 0; k < 4; k++) formatNumberRight(ip[k], ipFrames[k + 1]);
  ipFrameCount = 5;
  ipFramePos = 0;
  frameStartMs = millis();
  display.show(ipFrames[0]);
}

void handleDisplay() {
  if (buttonOverride) return;
  uint32_t now = millis();
  if (ipFrameCount > 0) {
    if (now - frameStartMs < 1500) return;
    frameStartMs = now;
    ipFramePos++;
    if (ipFramePos < ipFrameCount) {
      display.show(ipFrames[ipFramePos]);
      return;
    }
    ipFrameCount = 0;
    frameStartMs = 0;  // show temperatures right away
  }
  if (frameStartMs != 0 && now - frameStartMs < DISPLAY_FRAME_MS) return;
  frameStartMs = now ? now : 1;

  int list[NSLOTS];
  int n = 0;
  for (int i = 0; i < NSLOTS; i++)
    if (slots[i].configured) list[n++] = i;

  uint8_t segs[4];
  if (n == 0) {
    formatTemp(NAN, segs);
  } else {
    frameIndex = (frameIndex + 1) % n;
    formatTemp(slots[list[frameIndex]].temp, segs);
  }
  display.setBrightness(brightness);
  display.show(segs);
}

// ---------------------------------------------------------------- BOOT button
void factoryReset() {
  Serial.println("Werksreset");
  prefs.clear();
  const uint8_t dashes[4] = {seg::MINUS, seg::MINUS, seg::MINUS, seg::MINUS};
  display.show(dashes);
  delay(500);
  ESP.restart();
}

// Short press: show IP address. Hold 10 s: factory reset (countdown "rES7" ... "rES1").
void handleButton() {
  static bool wasDown = false;
  static uint32_t downSince = 0;
  static int lastLeft = -1;
  bool down = digitalRead(PIN_BOOT) == LOW;
  uint32_t now = millis();
  if (down && !wasDown) {
    downSince = now;
    lastLeft = -1;
  }
  if (down && now - downSince > 3000) {
    int left = (int)((RESET_HOLD_MS - (now - downSince) + 999) / 1000);
    if (now - downSince >= RESET_HOLD_MS) factoryReset();
    if (left != lastLeft) {
      lastLeft = left;
      buttonOverride = true;
      const uint8_t segs[4] = {seg::R, seg::E, seg::S, seg::DIGITS[left % 10]};
      display.show(segs);
    }
  }
  if (!down && wasDown) {
    uint32_t held = now - downSince;
    if (buttonOverride) {
      buttonOverride = false;  // released early: no reset
      frameStartMs = 0;
    } else if (held > 50 && held < 3000) {
      startIpDisplay();
    }
  }
  wasDown = down;
}

// ---------------------------------------------------------------- WiFi
void startSta() {
  if (staSsid.length() == 0) return;
  Serial.printf("Verbinde mit WLAN \"%s\"\n", staSsid.c_str());
#ifdef WOKWI_SIM
  WiFi.begin(staSsid.c_str(), staPass.c_str(), 6);  // Wokwi-GUEST is on channel 6
#else
  WiFi.begin(staSsid.c_str(), staPass.c_str());
#endif
  staLastTryMs = millis();
}

void apStart() {
#ifdef WOKWI_SIM
  return;  // Wokwi cannot simulate the hotspot
#endif
  if (apActive) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, apPass.c_str());
  dns.start(53, "*", WiFi.softAPIP());
  apActive = true;
  apOnSinceMs = millis();
  Serial.printf("Hotspot \"%s\" an, Seite: http://%s/\n", AP_SSID, WiFi.softAPIP().toString().c_str());
}

void apStop() {
  if (!apActive) return;
  dns.stop();
  WiFi.softAPdisconnect(true);  // keeps the home WiFi connection
  apActive = false;
  Serial.println("Hotspot aus (Heim-WLAN verbunden)");
}

void handleWifi() {
  uint32_t now = millis();
  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected != staWasConnected) staChangeMs = now;
  if (connected && !staWasConnected) {
    Serial.printf("WLAN verbunden, IP %s\n", WiFi.localIP().toString().c_str());
    configTzTime(TZ_INFO, "pool.ntp.org", "time.google.com");
    startIpDisplay();
  }
  staWasConnected = connected;

  // Hotspot: always on, or only for setup / as fallback when the home WiFi is down.
  bool want;
  if (!apAuto || staSsid.length() == 0) {
    want = true;
  } else if (connected) {
    // Count from the later of "home WiFi connected" and "hotspot (re)started / settings
    // changed", so a phone gets time to (re)join after a password change.
    uint32_t staUp = now - staChangeMs;
    uint32_t apUp = now - apOnSinceMs;
    uint32_t up = staUp < apUp ? staUp : apUp;
    bool phoneOnHotspot = apActive && WiFi.softAPgetStationNum() > 0;
    want = apActive && (up < AP_OFF_DELAY_MS || (phoneOnHotspot && up < AP_FORCE_OFF_MS));
  } else {
    want = apActive || now - staChangeMs > AP_FALLBACK_MS;
  }
  if (want && !apActive) apStart();
  if (!want && apActive) apStop();

  // Retry rarely: while the station searches for its network the hotspot is less stable.
  if (!connected && staSsid.length() > 0 && millis() - staLastTryMs > STA_RETRY_MS) {
    WiFi.disconnect(false, false);
    startSta();
  }
}

// ---------------------------------------------------------------- web
void sendJson(const String &body, int code = 200) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body);
}

// Every POST must carry the header "X-FM: 1". Browsers only send custom headers
// cross-origin after a CORS preflight, which this server never answers, so other
// web pages cannot trigger actions. The Host check blocks DNS rebinding.
bool requestAllowed() {
  String host = server.hostHeader();
  int colon = host.indexOf(':');
  if (colon >= 0) host = host.substring(0, colon);
  host.toLowerCase();
  bool hostOk = host == WiFi.softAPIP().toString() || host == WiFi.localIP().toString() || host == HOSTNAME;
#ifdef WOKWI_SIM
  if (host == "localhost") hostOk = true;  // port forwarding of the simulator
#endif
  // Names the home router may give the device, e.g. kuehlschrank.fritz.box
  static const char *suffixes[] = {".local", ".fritz.box", ".lan", ".home", ".home.arpa", ".box", ".speedport.ip"};
  for (const char *suf : suffixes)
    if (host == String(HOSTNAME) + suf) hostOk = true;
  return hostOk && server.header("X-FM") == "1";
}

bool checkRequest() {
  if (requestAllowed()) return true;
  sendJson("{\"ok\":false,\"error\":\"Anfrage abgelehnt\"}", 403);
  return false;
}

void handleState() {
  bool sta = WiFi.status() == WL_CONNECTED;
  String j;
  j.reserve(700);
  j += "{\"fw\":\"";
  j += FW_VERSION;
  j += "\",\"up\":";
  j += (uint32_t)(esp_timer_get_time() / 1000000ULL);
  j += ",\"epoch\":";
  j += timeValid() ? (uint32_t)time(nullptr) : 0;
  j += ",\"bright\":";
  j += brightness;
  j += ",\"ap\":{\"ssid\":\"";
  j += AP_SSID;
  j += "\",\"ip\":\"";
  j += WiFi.softAPIP().toString();
  j += "\",\"clients\":";
  j += apActive ? WiFi.softAPgetStationNum() : 0;
  j += ",\"active\":";
  j += apActive ? "true" : "false";
  j += ",\"auto\":";
  j += apAuto ? "true" : "false";
  j += ",\"defpass\":";
  j += apPass == AP_PASS_DEFAULT ? "true" : "false";
  j += "},\"sta\":{\"ssid\":\"";
  j += jsonEscape(staSsid);
  j += "\",\"connected\":";
  j += sta ? "true" : "false";
  j += ",\"ip\":\"";
  j += sta ? WiFi.localIP().toString() : "";
  j += "\",\"rssi\":";
  j += sta ? WiFi.RSSI() : 0;
  j += "},\"sensors\":[";
  for (int i = 0; i < NSLOTS; i++) {
    const Slot &s = slots[i];
    if (i) j += ",";
    j += "{\"configured\":";
    j += s.configured ? "true" : "false";
    j += ",\"present\":";
    j += s.present ? "true" : "false";
    j += ",\"name\":\"";
    j += jsonEscape(s.name);
    j += "\",\"addr\":\"";
    j += s.configured ? addrToString(s.addr) : "";
    j += "\",\"t\":";
    if (isnan(s.temp)) {
      j += "null";
    } else {
      char buf[12];
      snprintf(buf, sizeof(buf), "%.2f", s.temp);
      j += buf;
    }
    j += "}";
  }
  j += "]}";
  sendJson(j);
}

void handleHistory() {
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  String chunk;
  chunk.reserve(1200);
  chunk += "{\"interval\":60,\"age\":";
  chunk += (millis() - lastSampleMs) / 1000;
  chunk += ",\"epoch\":";
  chunk += timeValid() ? (uint32_t)time(nullptr) : 0;
  chunk += ",\"data\":[";
  uint16_t start = (histHead + HIST_LEN - histCount) % HIST_LEN;
  for (int i = 0; i < NSLOTS; i++) {
    chunk += i ? ",[" : "[";
    for (uint16_t k = 0; k < histCount; k++) {
      if (k) chunk += ",";
      int16_t v = hist[i][(start + k) % HIST_LEN];
      if (v == NODATA) chunk += "null";
      else chunk += v;
      if (chunk.length() > 1000) {
        server.sendContent(chunk);
        chunk = "";
      }
    }
    chunk += "]";
  }
  chunk += "]}";
  server.sendContent(chunk);
  server.sendContent("");  // end chunked response
}

void handleSettings() {
  if (!checkRequest()) return;
  if (server.hasArg("appass") && server.arg("appass").length() > 0) {
    size_t len = server.arg("appass").length();
    if (len < 8 || len > 63) {
      sendJson("{\"ok\":false,\"error\":\"Das Hotspot-Passwort muss 8 bis 63 Zeichen lang sein.\"}", 400);
      return;
    }
  }
  for (int i = 0; i < NSLOTS; i++) {
    String key = "name" + String(i);
    if (server.hasArg(key)) {
      slots[i].name = cleanName(server.arg(key));
      saveSlot(i);
    }
  }
  if (server.hasArg("bright")) {
    int b = server.arg("bright").toInt();
    brightness = (uint8_t)constrain(b, 0, 7);
    prefs.putUChar("bright", brightness);
    frameStartMs = 0;  // apply now
  }
  if (server.hasArg("apauto")) {
    bool v = server.arg("apauto") == "1";
    if (v != apAuto) apOnSinceMs = millis();  // give phones the full delay again
    apAuto = v;
    prefs.putBool("apauto", apAuto);
  }
  bool newPass = false;
  if (server.hasArg("appass") && server.arg("appass").length() > 0) {
    String p = server.arg("appass");
    newPass = p != apPass;
    apPass = p;
    prefs.putString("appass", apPass);
  }
  sendJson("{\"ok\":true}");
  if (newPass && apActive) {  // restart hotspot with the new password
    delay(300);
    apStop();
    apStart();
  }
}

void handleFactoryReset() {
  if (!checkRequest()) return;
  sendJson("{\"ok\":true}");
  delay(300);
  factoryReset();
}

void handleSwap() {
  if (!checkRequest()) return;
  Slot tmp = slots[0];
  slots[0] = slots[1];
  slots[1] = tmp;
  for (int k = 0; k < HIST_LEN; k++) {
    int16_t v = hist[0][k];
    hist[0][k] = hist[1][k];
    hist[1][k] = v;
  }
  saveSlot(0);
  saveSlot(1);
  sendJson("{\"ok\":true}");
}

void handleForget() {
  if (!checkRequest()) return;
  int i = server.hasArg("slot") ? server.arg("slot").toInt() : -1;
  if (!server.hasArg("slot") || i < 0 || i >= NSLOTS) {
    sendJson("{\"ok\":false}", 400);
    return;
  }
  slots[i] = Slot();
  clearHistorySlot(i);
  saveSlot(i);
  lastRescanMs = 0;  // look for probes again
  sendJson("{\"ok\":true}");
}

void handleWifiSave() {
  if (!checkRequest()) return;
  String ssid = server.arg("ssid");
  ssid.trim();
  String pass = server.arg("pass");
  if (ssid.length() > 32) {
    sendJson("{\"ok\":false,\"error\":\"Der WLAN-Name ist zu lang (max. 32 Zeichen).\"}", 400);
    return;
  }
  if (ssid.length() > 0 && pass.length() > 0 && (pass.length() < 8 || pass.length() > 63)) {
    sendJson("{\"ok\":false,\"error\":\"Das WLAN-Passwort muss 8 bis 63 Zeichen lang sein.\"}", 400);
    return;
  }
  staSsid = ssid;
  staPass = pass;
  prefs.putString("ssid", staSsid);
  prefs.putString("pass", staPass);
  sendJson("{\"ok\":true}");
  delay(100);
  WiFi.disconnect(false, false);
  if (staSsid.length() > 0) startSta();
}

void handleWifiScan() {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_FAILED) {
    WiFi.scanNetworks(true);
    sendJson("{\"busy\":true}");
    return;
  }
  if (n == WIFI_SCAN_RUNNING) {
    sendJson("{\"busy\":true}");
    return;
  }
  String j = "{\"busy\":false,\"nets\":[";
  int added = 0;
  for (int k = 0; k < n && added < 20; k++) {
    String ssid = WiFi.SSID(k);
    if (ssid.length() == 0 || ssid == AP_SSID) continue;
    if (added++) j += ",";
    j += "{\"ssid\":\"" + jsonEscape(ssid) + "\",\"rssi\":" + String(WiFi.RSSI(k)) + "}";
  }
  j += "]}";
  WiFi.scanDelete();
  sendJson(j);
}

void handleSetTime() {
  if (!checkRequest()) return;
  // The browser sends its clock so the history has real times even without internet.
  uint32_t t = strtoul(server.arg("t").c_str(), nullptr, 10);
  if (t > 1700000000UL) {
    long diff = (long)t - (long)time(nullptr);
    if (!timeValid() || labs(diff) > 120) {
      struct timeval tv = {(time_t)t, 0};
      settimeofday(&tv, nullptr);
    }
  }
  sendJson("{\"ok\":true}");
}

void handleUpdateDone() {
  if (uploadRejected) {
    uploadRejected = false;
    server.send(403, "text/plain", "Anfrage abgelehnt");
    return;
  }
  bool ok = !Update.hasError() && Update.isFinished();
  server.sendHeader("Connection", "close");
  server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : (Update.hasError() ? Update.errorString() : "Upload unvollstaendig"));
  if (ok) {
    delay(500);
    ESP.restart();
  }
}

void handleUpdateUpload() {
  esp_task_wdt_reset();  // a long upload is not a hang
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    uploadRejected = !requestAllowed();
    if (uploadRejected) return;
    Serial.printf("Update: %s\n", up.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Serial.printf("Update-Fehler: %s\n", Update.errorString());
  } else if (uploadRejected || Update.hasError()) {
    if (up.status == UPLOAD_FILE_ABORTED && Update.isRunning()) Update.abort();
    return;  // ignore the rest of a rejected or failed upload
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) {
      Serial.printf("Update-Fehler: %s\n", Update.errorString());
      Update.abort();
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (Update.end(true)) Serial.printf("Update fertig: %u Bytes\n", up.totalSize);
    else Serial.printf("Update-Fehler: %s\n", Update.errorString());
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
  }
}

void handleRoot() {
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", PAGE_HTML);
}

void handleNotFound() {
  // Captive portal: send phones that just joined the hotspot to the start page.
  if (apActive && server.client().localIP() == WiFi.softAPIP()) {
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
    return;
  }
  server.send(404, "text/plain", "Nicht gefunden");
}

void setupWeb() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/history", HTTP_GET, handleHistory);
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/api/swap", HTTP_POST, handleSwap);
  server.on("/api/forget", HTTP_POST, handleForget);
  server.on("/api/wifi", HTTP_POST, handleWifiSave);
  server.on("/api/scan", HTTP_GET, handleWifiScan);
  server.on("/api/time", HTTP_POST, handleSetTime);
  server.on("/api/reset", HTTP_POST, handleFactoryReset);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.onNotFound(handleNotFound);
  const char *headerKeys[] = {"X-FM"};
  server.collectHeaders(headerKeys, 1);
  server.begin();
}

// ---------------------------------------------------------------- setup/loop
void setup() {
  Serial.begin(115200);
  // Watchdog for the loop task: restarts the ESP if loop() hangs (e.g. a stalled upload).
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
  esp_task_wdt_add(NULL);
  Serial.printf("\nKuehlschrank-Monitor %s\n", FW_VERSION);

  setenv("TZ", TZ_INFO, 1);
  tzset();

  pinMode(PIN_BOOT, INPUT_PULLUP);
  pinMode(PIN_SENSOR_PWR, OUTPUT);
  digitalWrite(PIN_SENSOR_PWR, HIGH);

  display.begin();
  display.setBrightness(4);
  const uint8_t dashes[4] = {seg::MINUS, seg::MINUS, seg::MINUS, seg::MINUS};
  display.show(dashes);

  for (int i = 0; i < NSLOTS; i++) clearHistorySlot(i);
  loadSettings();

  delay(50);
  dallas.begin();
  dallas.setWaitForConversion(false);
  scanBus();
  lastSampleMs = millis();
  lastPowerCycleMs = millis();

  WiFi.persistent(false);
  WiFi.setHostname(HOSTNAME);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(false);  // retries are handled in handleWifi()
  apStart();  // on at boot; handleWifi() switches it off later if configured
  startSta();
  MDNS.begin(HOSTNAME);
  MDNS.addService("http", "tcp", 80);
  setupWeb();

  startIpDisplay();
}

void loop() {
  esp_task_wdt_reset();
  if (apActive) dns.processNextRequest();
  server.handleClient();
  handleSensors();
  handleWifi();
  handleButton();
  handleDisplay();
  delay(2);
}
