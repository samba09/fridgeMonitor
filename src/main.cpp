// Kühlschrank-Monitor
//
// ESP32 + 2x DS18B20 + TM1637 4-digit display.
// - Own WiFi hotspot "Kuehlschrank" (always on) with web page at http://192.168.4.1
// - Optional: additionally joins the home WiFi (configured on the web page)
// - Display alternates between the two probes (whole degrees)
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
#include <sys/time.h>
#include <time.h>

#include "tm1637.h"
#include "webpage.h"

static const char *FW_VERSION = "1.0.0";

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

static const char *AP_SSID = "Kuehlschrank";
static const char *AP_PASS = "kuehlschrank";  // min. 8 characters
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
bool staWasConnected = false;
uint32_t staLastTryMs = 0;

bool conversionPending = false;
uint32_t conversionStartMs = 0;
uint32_t lastReadMs = 0;
uint32_t lastRescanMs = 0;
uint8_t emptyScans = 0;

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

String cleanName(String s) {
  s.trim();
  if (s.length() > 24) s = s.substring(0, 24);
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

  if (now - lastSampleMs >= SAMPLE_INTERVAL_MS) {
    lastSampleMs += SAMPLE_INTERVAL_MS;
    if (now - lastSampleMs >= SAMPLE_INTERVAL_MS) lastSampleMs = now;  // don't catch up after stalls
    pushHistory();
  }
}

// ---------------------------------------------------------------- display
void formatTemp(float t, uint8_t out[4]) {
  for (int k = 0; k < 4; k++) out[k] = seg::BLANK;
  if (isnan(t)) {
    for (int k = 0; k < 4; k++) out[k] = seg::MINUS;
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
void handleButton() {
  static bool wasDown = false;
  static uint32_t downSince = 0;
  bool down = digitalRead(PIN_BOOT) == LOW;
  uint32_t now = millis();
  if (down && !wasDown) downSince = now;
  if (!down && wasDown && now - downSince > 50) startIpDisplay();
  wasDown = down;
}

// ---------------------------------------------------------------- WiFi
void startSta() {
  if (staSsid.length() == 0) return;
  Serial.printf("Verbinde mit WLAN \"%s\"\n", staSsid.c_str());
  WiFi.begin(staSsid.c_str(), staPass.c_str());
  staLastTryMs = millis();
}

void handleWifi() {
  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && !staWasConnected) {
    Serial.printf("WLAN verbunden, IP %s\n", WiFi.localIP().toString().c_str());
    configTzTime(TZ_INFO, "pool.ntp.org", "time.google.com");
    startIpDisplay();
  }
  staWasConnected = connected;
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

void handleState() {
  bool sta = WiFi.status() == WL_CONNECTED;
  String j;
  j.reserve(700);
  j += "{\"fw\":\"";
  j += FW_VERSION;
  j += "\",\"up\":";
  j += millis() / 1000;
  j += ",\"epoch\":";
  j += timeValid() ? (uint32_t)time(nullptr) : 0;
  j += ",\"bright\":";
  j += brightness;
  j += ",\"ap\":{\"ssid\":\"";
  j += AP_SSID;
  j += "\",\"ip\":\"";
  j += WiFi.softAPIP().toString();
  j += "\",\"clients\":";
  j += WiFi.softAPgetStationNum();
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
  sendJson("{\"ok\":true}");
}

void handleSwap() {
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
  int i = server.arg("slot").toInt();
  if (i < 0 || i >= NSLOTS) {
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
  staSsid = server.arg("ssid");
  staSsid.trim();
  staPass = server.arg("pass");
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
  bool ok = !Update.hasError();
  server.sendHeader("Connection", "close");
  server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : Update.errorString());
  if (ok) {
    delay(500);
    ESP.restart();
  }
}

void handleUpdateUpload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    Serial.printf("Update: %s\n", up.filename.c_str());
    Update.begin(UPDATE_SIZE_UNKNOWN);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    Update.write(up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END) {
    Update.end(true);
    Serial.printf("Update fertig: %u Bytes\n", up.totalSize);
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
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "");
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
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.onNotFound(handleNotFound);
  server.begin();
}

// ---------------------------------------------------------------- setup/loop
void setup() {
  Serial.begin(115200);
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

  WiFi.persistent(false);
  WiFi.setHostname(HOSTNAME);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(false);  // retries are handled in handleWifi()
  WiFi.softAP(AP_SSID, AP_PASS);
  dns.start(53, "*", WiFi.softAPIP());
  startSta();
  MDNS.begin(HOSTNAME);
  MDNS.addService("http", "tcp", 80);
  setupWeb();

  Serial.printf("Hotspot \"%s\" (Passwort \"%s\"), Seite: http://%s/\n", AP_SSID, AP_PASS,
                WiFi.softAPIP().toString().c_str());
  startIpDisplay();
}

void loop() {
  dns.processNextRequest();
  server.handleClient();
  handleSensors();
  handleWifi();
  handleButton();
  handleDisplay();
  delay(2);
}
