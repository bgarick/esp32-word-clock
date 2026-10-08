/*
  ESP32-C3 Super Mini — Word Clock + WiFiManager (SSID + Timezone + Birthday + Layout)
  With verbose serial debug logging.

  Changes from original ESP32 version:
    - LED_PIN  changed from 13 -> 4   (safe output on C3)
    - BTN_PIN  changed from  0 -> 9   (BOOT button is GPIO9 on C3)
    - Serial startup: delay(1000) + while(!Serial) for native USB CDC
    - Removed esp_wifi_set_ps(WIFI_PS_NONE) — not needed / problematic on C3
    - Kept esp_wifi.h for esp_wifi_restore() only (esp_wifi_connect removed)
    - Board: ESP32C3 Dev Module, USB CDC On Boot: Enabled

  v2 changes:
    - Removed brightness slider; brightness fixed at default (15)
    - Birthday month/day now configurable via portal (saved to Preferences)

  v3 changes:
    - Layout (Vertical / Horizontal) selectable via portal (saved to Preferences)

  v11 changes:
    - One source, two builds. Target is picked at compile time:
        ESP32-C3 (Super Mini)      -> LED GPIO4, button GPIO9, esp32-word-clock.bin
        classic ESP32 (Dev Module) -> LED GPIO4 (selectable in portal), button GPIO0,
                                      esp32-word-clock-esp32.bin
    - Classic ESP32: LED data pin is selectable in the setup portal (safe GPIOs only).
    - OTA downloads the binary that matches the chip it is running on.
*/

#include <WiFi.h>
#include <WiFiManager.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <string.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>

#define NUMPIXELS  144
#define DELAYVAL   30000

// ---- Per-chip build target (chosen automatically by the selected board) ----
#if defined(CONFIG_IDF_TARGET_ESP32C3)
  #define BOARD_NAME        "ESP32-C3"
  #define DEFAULT_LED_PIN   4
  #define BTN_PIN           9                          // BOOT button on C3 Super Mini
  #define FW_BIN_NAME       "esp32-word-clock.bin"     // name kept for clocks already in the field
#elif defined(CONFIG_IDF_TARGET_ESP32)
  #define BOARD_NAME        "ESP32"
  #define DEFAULT_LED_PIN   4
  #define BTN_PIN           0                          // BOOT button on classic ESP32 dev boards
  #define FW_BIN_NAME       "esp32-word-clock-esp32.bin"
  #define LED_PIN_SELECTABLE 1                         // pin picker shown in setup portal
#else
  #error "Unsupported chip: build for ESP32-C3 or classic ESP32"
#endif

uint8_t ledPin = DEFAULT_LED_PIN;       // may be overridden from Preferences (classic ESP32 only)
bool    pinChangePending = false;       // set by portal save; triggers a reboot to apply

#ifdef LED_PIN_SELECTABLE
// Output-capable GPIOs that are not flash pins, input-only pins, or boot-strapping pins.
static const uint8_t LED_PIN_CHOICES[] = {4, 13, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33};
static bool validLedPin(int p) {
  for (uint8_t c : LED_PIN_CHOICES) if (c == p) return true;
  return false;
}
#endif

Adafruit_NeoPixel pixels(NUMPIXELS, DEFAULT_LED_PIN, NEO_GRB + NEO_KHZ800);

Preferences prefs;
const char* ntpServerA = "time.google.com";
const char* ntpServerB = "pool.ntp.org";
const char* ntpServerC = "time.windows.com";

bool   debug   = true;
tm     tm_now;
time_t nowEpoch;
int    year_, month_, day_, hour_, minute_;
int    r = 0, g = 0, b = 0;
uint16_t frameLitCount = 0;
uint8_t brightness = 15;   // fixed default; no longer user-configurable

// ============================================================================
// Fade / transition state  (added 2026-10-03: smooth staggered time changes)
// ============================================================================
#define FADE_OUT_MS 250     // outgoing words fade to black
#define FADE_IN_MS  320     // each incoming group fades up (slower = gentler)
#define FADE_STEPS  20      // interpolation steps per fade
uint8_t live[NUMPIXELS][3]; // colors currently on the strip (final RGB)
uint8_t tgt [NUMPIXELS][3]; // target frame built by showTime()
uint8_t grp [NUMPIXELS];    // per-pixel group: 0=none 1=hour 2=minute 3=connector
uint8_t g_curGroup = 0;     // group currently being painted by setLEDs()

// ============================================================================
// Special Days (1-5): each a date + mode (3 colors or rainbow) + 3 colors.
// Day 1 replaces the old "birthday". On a matching date the clock uses the
// day's look, overriding the seasonal/holiday palette.
// ============================================================================
#define SD_COLORS   0
#define SD_RAINBOW  1
#define NUM_SPECIAL 5
struct SpecialDay { uint8_t month; uint8_t day; uint8_t mode; uint32_t c1, c2, c3; };
SpecialDay special[NUM_SPECIAL];
int g_activeSpecial = -1;   // index of today's special day, or -1

// Layout: 0 = Vertical, 1 = Horizontal
uint8_t layout = 0;

// ============================================================================
// OTA auto-update (added 2026-10-03)
// Bump FW_VERSION with every release. The GitHub Action publishes version.json
// to Pages and the firmware .bin to the latest Release; the clock updates when
// the published version is higher than the one baked in here.
// ============================================================================
#define FW_VERSION 11
const char* OTA_VERSION_URL  = "https://bgarick.github.io/esp32-word-clock/version.json";
// Each chip downloads its own binary (a C3 image will not boot on a classic ESP32 and vice versa).
const char* OTA_FIRMWARE_URL = "https://github.com/bgarick/esp32-word-clock/releases/latest/download/" FW_BIN_NAME;
int  otaLastCheckYday = -1;   // day-of-year of last daily check (avoid repeats)

volatile bool WIFI_OK = false;
volatile uint8_t WIFI_LAST_REASON = 0;

String ssidHtml;

#define JULY4_PER_LETTER   1
#define RAINBOW_PER_LETTER 1

static uint8_t g_forcePalette = 0;
static bool    g_forceRainbow  = false;
static bool    g_forceJuly4    = false;

// ============================================================================
// Word index arrays — Vertical (layout == 0)
// ============================================================================
int V_oneMin[]     = { 123,115,116 };
int V_twoMin[]     = { 121,122,123 };
int V_threeMin[]   = { 140,139,132,131,124 };
int V_fourMin[]    = { 111,104,103,96 };
int V_fiveMin[]    = { 108,109,110,111 };
int V_sixMin[]     = { 95,88,87 };
int V_sevenMin[]   = { 142,137,134,129,126 };
int V_eightMin[]   = { 141,138,133,130,125 };
int V_nineMin[]    = { 91,84,83,76 };
int V_tenMin[]     = { 116,117,118 };
int V_elevenMin[]  = { 108,107,100,99,92,91 };
int V_twelveMin[]  = { 125,122,117,114,109,106 };
int V_thirteen[]   = { 118,113,110,105,80,81,82,83 };
int V_fourteen[]   = { 111,104,103,96,80,81,82,83 };
int V_fifteen[]    = { 119,112,111,80,81,82,83 };
int V_quarter[]    = { 102,97,94,89,86,81,78 };   // FIX 2026-10-03: was {101,98,93,90,85,82,77} (overlapped HALF; shifted one column)
int V_sixteen[]    = { 95,88,87,80,81,82,83 };
int V_seventeen[]  = { 142,137,134,129,126,80,81,82,83 };
int V_eighteen[]   = { 141,138,133,130,80,81,82,83 };
int V_nineteen[]   = { 91,84,76,80,81,82,83 };
int V_twenty[]     = { 143,136,135,128,127,120 };
int V_half_[]      = { 101,98,93,90 };
int V_after[]      = { 70,65,62,57,54 };
int V_past[]       = { 49,46,41,38 };
int V_til[]        = { 73,74,75 };
int V_of[]         = { 64,65 };
int V_to_[]        = { 68,69 };
int V_before[]     = { 79,72,71,64,63,56 };
int V_oneHour[]    = { 43,36,35 };
int V_twoHour[]    = { 4,5,6 };
int V_threeHour[]  = { 66,61,58,53,50 };
int V_fourHour[]   = { 31,24,23,16 };
int V_fiveHour[]   = { 28,29,30,31 };
int V_sixHour[]    = { 1,2,3 };
int V_sevenHour[]  = { 67,60,59,52,51 };
int V_eightHour[]  = { 26,21,18,13,10 };
int V_nineHour[]   = { 15,8,7,0 };
int V_tenHour[]    = { 38,37,36 };
int V_elevenHour[] = { 28,27,20,19,12,11 };
int V_twelveHour[] = { 45,42,37,34,29,26 };
int V_noon[]       = { 51,44,43,36 };
int V_midnight[]   = { 33,30,25,22,17,14,9,6 };
int V_oclock[]     = { 55,48,47,40,39,32 };
int V_wifiAnim[]   = { 5,2,31,30 };
int V_noAnim[]     = { 51,44 };

// ============================================================================
// Word index arrays — Horizontal (layout == 1)
// ============================================================================
int H_oneMin[]     = { 120,119,112 };
int H_twoMin[]     = { 120,121,122 };
int H_threeMin[]   = { 143,136,135,128,127 };
int H_fourMin[]    = { 99,100,107,108 };
int H_fiveMin[]    = { 108,109,110,111 };
int H_sixMin[]     = { 92,91,84 };
int H_sevenMin[]   = { 141,138,133,130,125 };
int H_eightMin[]   = { 142,137,134,129,126 };
int H_nineMin[]    = { 79,80,87,88 };
int H_tenMin[]     = { 117,118,119 };
int H_elevenMin[]  = { 111,104,103,96,95,88 };
int H_twelveMin[]  = { 126,121,118,113,110,105 };
int H_thirteen[]   = { 117,114,109,106,80,81,82,83 };
int H_fourteen[]   = { 99,100,107,108,80,81,82,83 };
int H_fifteen[]    = { 116,115,108,80,81,82,83 };
int H_quarter[]    = { 101,98,93,90,85,82,77 };   // FIX 2026-10-03: was {102,97,94,89,86,81,78} (overlapped HALF; shifted one column)
int H_sixteen[]    = { 92,91,84,80,81,82,83 };
int H_seventeen[]  = { 141,138,133,130,125,80,81,82,83 };
int H_eighteen[]   = { 142,137,134,129,80,81,82,83 };
int H_nineteen[]   = { 79,80,87,88,80,81,82,83 };
int H_twenty[]     = { 140,139,132,131,124,123 };
int H_half_[]      = { 102,97,94,89 };
int H_after[]      = { 69,66,61,58,53 };
int H_past[]       = { 50,45,42,37 };
int H_til[]        = { 72,73,74 };
int H_of[]         = { 66,67 };
int H_to_[]        = { 70,71 };
int H_before[]     = { 76,75,68,67,60,59 };
int H_oneHour[]    = { 40,39,32 };
int H_twoHour[]    = { 5,6,7 };
int H_threeHour[]  = { 65,62,57,54,49 };
int H_fourHour[]   = { 28,27,20,19 };
int H_fiveHour[]   = { 28,29,30,31 };
int H_sixHour[]    = { 0,1,2 };
int H_sevenHour[]  = { 64,63,56,55,48 };
int H_eightHour[]  = { 25,22,17,14,9 };
int H_nineHour[]   = { 12,11,4,3 };
int H_tenHour[]    = { 39,38,37 };
int H_elevenHour[] = { 31,24,23,16,15,8 };
int H_twelveHour[] = { 46,41,38,33,30,25 };
int H_noon[]       = { 48,47,40,39 };
int H_midnight[]   = { 34,29,26,21,18,13,10,5 };
int H_oclock[]     = { 52,51,44,43,36,35 };
int H_wifiAnim[]   = { 78,73,28,29 };
int H_noAnim[]     = { 48,47 };

// ============================================================================
// Active pointers — set by applyLayout()
// ============================================================================
int* oneMin;
int* twoMin;
int* threeMin;
int* fourMin;
int* fiveMin;
int* sixMin;
int* sevenMin;
int* eightMin;
int* nineMin;
int* tenMin;
int* elevenMin;
int* twelveMin;
int* thirteen;
int* fourteen;
int* fifteen;
int* quarter;
int* sixteen;
int* seventeen;
int* eighteen;
int* nineteen;
int* twenty;
int* half_;
int* after;
int* past;
int* til;
int* of;
int* to_;
int* before;
int* oneHour;
int* twoHour;
int* threeHour;
int* fourHour;
int* fiveHour;
int* sixHour;
int* sevenHour;
int* eightHour;
int* nineHour;
int* tenHour;
int* elevenHour;
int* twelveHour;
int* noon;
int* midnight;
int* oclock;
int* wifiAnim;
int* noAnim;

// Word lengths — same for both layouts
const int LEN_oneMin     = 3;
const int LEN_twoMin     = 3;
const int LEN_threeMin   = 5;
const int LEN_fourMin    = 4;
const int LEN_fiveMin    = 4;
const int LEN_sixMin     = 3;
const int LEN_sevenMin   = 5;
const int LEN_eightMin   = 5;
const int LEN_nineMin    = 4;
const int LEN_tenMin     = 3;
const int LEN_elevenMin  = 6;
const int LEN_twelveMin  = 6;
const int LEN_thirteen   = 8;
const int LEN_fourteen   = 8;
const int LEN_fifteen    = 7;
const int LEN_quarter    = 7;
const int LEN_sixteen    = 7;
const int LEN_seventeen  = 9;
const int LEN_eighteen   = 8;
const int LEN_nineteen   = 8;
const int LEN_twenty     = 6;
const int LEN_half_      = 4;
const int LEN_after      = 5;
const int LEN_past       = 4;
const int LEN_til        = 3;
const int LEN_of         = 2;
const int LEN_to_        = 2;
const int LEN_before     = 6;
const int LEN_oneHour    = 3;
const int LEN_twoHour    = 3;
const int LEN_threeHour  = 5;
const int LEN_fourHour   = 4;
const int LEN_fiveHour   = 4;
const int LEN_sixHour    = 3;
const int LEN_sevenHour  = 5;
const int LEN_eightHour  = 5;
const int LEN_nineHour   = 4;
const int LEN_tenHour    = 3;
const int LEN_elevenHour = 6;
const int LEN_twelveHour = 6;
const int LEN_noon       = 4;
const int LEN_midnight   = 8;
const int LEN_oclock     = 6;
const int LEN_wifiAnim   = 4;
const int LEN_noAnim     = 2;

void applyLayout() {
  if (layout == 0) {
    oneMin     = V_oneMin;     twoMin     = V_twoMin;
    threeMin   = V_threeMin;   fourMin    = V_fourMin;
    fiveMin    = V_fiveMin;    sixMin     = V_sixMin;
    sevenMin   = V_sevenMin;   eightMin   = V_eightMin;
    nineMin    = V_nineMin;    tenMin     = V_tenMin;
    elevenMin  = V_elevenMin;  twelveMin  = V_twelveMin;
    thirteen   = V_thirteen;   fourteen   = V_fourteen;
    fifteen    = V_fifteen;    quarter    = V_quarter;
    sixteen    = V_sixteen;    seventeen  = V_seventeen;
    eighteen   = V_eighteen;   nineteen   = V_nineteen;
    twenty     = V_twenty;     half_      = V_half_;
    after      = V_after;      past       = V_past;
    til        = V_til;        of         = V_of;
    to_        = V_to_;        before     = V_before;
    oneHour    = V_oneHour;    twoHour    = V_twoHour;
    threeHour  = V_threeHour;  fourHour   = V_fourHour;
    fiveHour   = V_fiveHour;   sixHour    = V_sixHour;
    sevenHour  = V_sevenHour;  eightHour  = V_eightHour;
    nineHour   = V_nineHour;   tenHour    = V_tenHour;
    elevenHour = V_elevenHour; twelveHour = V_twelveHour;
    noon       = V_noon;       midnight   = V_midnight;
    oclock     = V_oclock;     wifiAnim   = V_wifiAnim;
    noAnim     = V_noAnim;
  } else {
    oneMin     = H_oneMin;     twoMin     = H_twoMin;
    threeMin   = H_threeMin;   fourMin    = H_fourMin;
    fiveMin    = H_fiveMin;    sixMin     = H_sixMin;
    sevenMin   = H_sevenMin;   eightMin   = H_eightMin;
    nineMin    = H_nineMin;    tenMin     = H_tenMin;
    elevenMin  = H_elevenMin;  twelveMin  = H_twelveMin;
    thirteen   = H_thirteen;   fourteen   = H_fourteen;
    fifteen    = H_fifteen;    quarter    = H_quarter;
    sixteen    = H_sixteen;    seventeen  = H_seventeen;
    eighteen   = H_eighteen;   nineteen   = H_nineteen;
    twenty     = H_twenty;     half_      = H_half_;
    after      = H_after;      past       = H_past;
    til        = H_til;        of         = H_of;
    to_        = H_to_;        before     = H_before;
    oneHour    = H_oneHour;    twoHour    = H_twoHour;
    threeHour  = H_threeHour;  fourHour   = H_fourHour;
    fiveHour   = H_fiveHour;   sixHour    = H_sixHour;
    sevenHour  = H_sevenHour;  eightHour  = H_eightHour;
    nineHour   = H_nineHour;   tenHour    = H_tenHour;
    elevenHour = H_elevenHour; twelveHour = H_twelveHour;
    noon       = H_noon;       midnight   = H_midnight;
    oclock     = H_oclock;     wifiAnim   = H_wifiAnim;
    noAnim     = H_noAnim;
  }
  Serial.printf("[LAYOUT] Active layout: %s\n", layout == 0 ? "Vertical" : "Horizontal");
}

void applyTimezone(const char* tz);
void ensureTimeSynced();
void setColor(int order);
void showTime(int hour, int minute);
void setLEDs(int a[], int len);
uint8_t gamma8(uint8_t v);
void animateIndices(const int* seq, int len, uint16_t hold_ms);
void runPortalAnimation(WiFiManager& wm);
void applyPowerLimitAndShow();
void transitionToTime(int h, int m);
void checkForUpdate(const char* reason);

static uint32_t wheel(uint8_t pos){
  pos = 255 - pos;
  uint8_t R,G,B;
  if (pos < 85)      { R=255-pos*3; G=0;        B=pos*3;      }
  else if (pos <170) { pos-=85;     R=0;         G=pos*3;      B=255-pos*3; }
  else               { pos-=170;    R=pos*3;     G=255-pos*3;  B=0;         }
  return ((uint32_t)R<<16) | ((uint32_t)G<<8) | B;
}
static inline void unpack(uint32_t c, uint8_t& rr, uint8_t& gg, uint8_t& bb){
  rr=(c>>16)&0xFF; gg=(c>>8)&0xFF; bb=c&0xFF;
}

bool isJuly4()      { return (month_ == 7  && day_ == 4); }
bool isPride()      { return (month_ == 6); }
bool isValentine()  { return (month_ == 2  && day_ == 14); }
bool isStPatrick()  { return (month_ == 3  && day_ == 17); }
bool isHalloween()  { return (month_ == 10); }
bool isChristmas()  { return (month_ == 12); }
bool isAprilFools() { return (month_ == 4  && day_ == 1); }

// ---- Special Day helpers ---------------------------------------------------
void initSpecialDefaults() {
  for (int i = 0; i < NUM_SPECIAL; i++) special[i] = {0, 0, SD_COLORS, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF};
  special[0] = {1, 25, SD_RAINBOW, 0xFF0000, 0x00FF00, 0x0000FF};  // Day 1 = old birthday (rainbow)
}
String serializeSpecial(const SpecialDay& sd) {
  char buf[48];
  snprintf(buf, sizeof(buf), "%u,%u,%u,%06lX,%06lX,%06lX",
           sd.month, sd.day, sd.mode,
           (unsigned long)(sd.c1 & 0xFFFFFF), (unsigned long)(sd.c2 & 0xFFFFFF), (unsigned long)(sd.c3 & 0xFFFFFF));
  return String(buf);
}
void parseSpecial(const String& s, SpecialDay& sd) {
  String f[6]; int n = 0, p = 0;
  for (int i = 0; i <= (int)s.length() && n < 6; i++) {
    if (i == (int)s.length() || s[i] == ',') { f[n++] = s.substring(p, i); p = i + 1; }
  }
  if (n >= 1) sd.month = (uint8_t)f[0].toInt();
  if (n >= 2) sd.day   = (uint8_t)f[1].toInt();
  if (n >= 3) sd.mode  = (uint8_t)f[2].toInt();
  if (n >= 4) sd.c1 = (uint32_t)strtoul(f[3].c_str(), nullptr, 16);
  if (n >= 5) sd.c2 = (uint32_t)strtoul(f[4].c_str(), nullptr, 16);
  if (n >= 6) sd.c3 = (uint32_t)strtoul(f[5].c_str(), nullptr, 16);
  if (sd.month > 12) sd.month = 0;
  if (sd.day < 1 || sd.day > 31) sd.day = 1;
  if (sd.mode > 1) sd.mode = SD_COLORS;
}
// Call with prefs already begun (read). Migrates the old birthday into Day 1.
void loadSpecialDays() {
  initSpecialDefaults();
  special[0].month = prefs.getUChar("bdayMonth", special[0].month);
  special[0].day   = prefs.getUChar("bdayDay",   special[0].day);
  for (int i = 0; i < NUM_SPECIAL; i++) {
    String key = "sd" + String(i + 1);
    String def = serializeSpecial(special[i]);
    parseSpecial(prefs.getString(key.c_str(), def), special[i]);
  }
}
int activeSpecialDay() {
  for (int i = 0; i < NUM_SPECIAL; i++)
    if (special[i].month >= 1 && special[i].month == month_ && special[i].day == day_) return i;
  return -1;
}

int thanksgivingDay(int yr, int wdayNov1){
  int firstThu = ((4 - wdayNov1 + 7) % 7) + 1;
  return firstThu + 21;
}
bool isThanksgiving(){
  if (month_ != 11) return false;
  struct tm t{};
  t.tm_year = year_ - 1900;
  t.tm_mon  = 10;
  t.tm_mday = 1;
  t.tm_hour = 12;
  time_t e = mktime(&t);
  struct tm out{};
  localtime_r(&e, &out);
  return (day_ == thanksgivingDay(year_, out.tm_wday));
}

// ============================================================================
// Wi-Fi event handler
// ============================================================================
void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_START:
      Serial.println("[WIFI] STA started.");
      WiFi.setSleep(false);
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("[WIFI] Associated with AP.");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      WIFI_OK = true;
      Serial.printf("[WIFI] Got IP -- SSID: %s  IP: %s  RSSI: %d dBm\n",
        WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      WIFI_OK = false;
      WIFI_LAST_REASON = info.wifi_sta_disconnected.reason;
      Serial.printf("[WIFI] Disconnected -- reason=%u. Auto-reconnect will retry.\n",
        (unsigned)WIFI_LAST_REASON);
      break;
    default: break;
  }
}

// ============================================================================
// setup()
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println("\n\n========================================");
  Serial.println("       Word Clock -- " BOARD_NAME " Boot");
  Serial.println("========================================");
  Serial.printf("  LED_PIN  : GPIO%d (default)\n", DEFAULT_LED_PIN);
  Serial.printf("  BTN_PIN  : GPIO%d\n", BTN_PIN);
  Serial.printf("  NeoPixels: %d\n", NUMPIXELS);
  Serial.println("----------------------------------------");

  pinMode(BTN_PIN, INPUT_PULLUP);
  bool forcePortalAtBoot = false;

  Serial.println("[BOOT] Waiting 2s -- press BOOT to open portal, hold 5s to clear credentials...");
  bool buttonPressed = false;
  uint32_t phase1Start = millis();
  while (millis() - phase1Start < 2000) {
    if (digitalRead(BTN_PIN) == LOW) { buttonPressed = true; break; }
    delay(10);
  }

  if (!buttonPressed) {
    Serial.println("[BOOT] No button press -- normal boot.");
  } else {
    Serial.println("[BOOT] Button pressed! Hold for 5s to clear credentials, release for portal only...");
    uint32_t holdStart = millis();
    bool longHold = true;
    while (millis() - holdStart < 5000) {
      if (digitalRead(BTN_PIN) == HIGH) { longHold = false; break; }
      delay(10);
    }
    if (longHold) {
      Serial.println("[BOOT] Long hold detected -- clearing Wi-Fi credentials...");
      WiFi.mode(WIFI_STA);
      delay(50);
      WiFi.disconnect(true, true);
      esp_wifi_restore();
      delay(200);
      WiFi.mode(WIFI_OFF);
      delay(100);
      Serial.println("[BOOT] Credentials cleared. Portal will open.");
      forcePortalAtBoot = true;
    } else {
      Serial.println("[BOOT] Short press -- opening portal without credential erase.");
      forcePortalAtBoot = true;
    }
  }

  // --- Load saved preferences ---
  Serial.println("[PREFS] Loading saved settings...");
  prefs.begin("settings", true);
  String storedTZ = prefs.getString("tz", "EST5EDT,M3.2.0/2,M11.1.0/2");
  layout    = prefs.getUChar("layout",    0);
#ifdef LED_PIN_SELECTABLE
  {
    int p = prefs.getUChar("ledPin", DEFAULT_LED_PIN);
    ledPin = validLedPin(p) ? (uint8_t)p : DEFAULT_LED_PIN;
  }
#endif
  loadSpecialDays();
  prefs.end();

  if (layout > 1) layout = 0;

  Serial.printf("[PREFS] TZ: %s  SpecialDay1: %02u/%02u  Layout: %s\n",
    storedTZ.c_str(), special[0].month, special[0].day, layout == 0 ? "Vertical" : "Horizontal");

  applyLayout();

  // --- NeoPixel init ---
  Serial.printf("[LED] Initializing NeoPixels on GPIO%d...\n", ledPin);
  pixels.setPin(ledPin);
  pixels.begin();
  pixels.setBrightness(brightness);
  pixels.clear();
  pixels.show();
  Serial.println("[LED] NeoPixels ready.");

  randomSeed(esp_random());

// --- Wi-Fi stack ---
  Serial.println("[WIFI] Configuring Wi-Fi stack...");
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(200);
  WiFi.mode(WIFI_STA);
  delay(500);
  WiFi.persistent(true);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.setHostname("WordClock");
  WiFi.onEvent(onWiFiEvent);
  Serial.println("[WIFI] Stack configured. Hostname: WordClock");

  delay(500);

  delay(500);  // <-- add this; gives radio time to settle after credential wipe


  // --- Network scan ---
  Serial.println("[WIFI] Scanning for networks...");
  int n = WiFi.scanNetworks(false, true);
  Serial.printf("[WIFI] Scan complete -- %d network(s) found:\n", n);

  ssidHtml  = "<label for='ssid_select'>WiFi Network:</label>";
  ssidHtml += "<select id='ssid_select' style='width:100%;padding:6px;'>";
  if (n <= 0) {
    Serial.println("[WIFI]   (none found)");
    ssidHtml += "<option value=''>No networks found</option>";
  } else {
    for (int i = 0; i < n; ++i) {
      String ssid = WiFi.SSID(i);
      int rssi    = WiFi.RSSI(i);
      bool enc    = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
      Serial.printf("[WIFI]   %2d: %-32s  RSSI: %4d  %s\n",
        i+1, ssid.c_str(), rssi, enc ? "secured" : "open");
      ssidHtml += "<option value='" + ssid + "'>" + ssid + " (" + String(rssi);
      ssidHtml += enc ? ", locked" : ", open";
      ssidHtml += ")</option>";
    }
  }
  ssidHtml += "</select>";
  ssidHtml += R"rawliteral(
    <script>
      document.addEventListener('DOMContentLoaded', function(){
        var sel = document.getElementById('ssid_select');
        var s   = document.getElementsByName('s')[0];
        if(s && sel){ s.value = sel.value; }
        if(sel){
          sel.addEventListener('change', function(){ if(s){ s.value = this.value; } });
        }
      });
    </script>
    <br/><br/>
  )rawliteral";

  // --- WiFiManager ---
  Serial.println("[WIFI] Configuring WiFiManager...");
  WiFiManager wm;

  WiFiManagerParameter ssidDropdown(ssidHtml.c_str());
  wm.addParameter(&ssidDropdown);

  WiFiManagerParameter tzParam("tz", "POSIX TZ", storedTZ.c_str(), 80, "type='hidden'");
  wm.addParameter(&tzParam);

  String tz_html = R"rawliteral(
    <label for='tzsel'>Timezone:</label>
    <select id='tzsel' style='width:100%;padding:6px;'>
      <option value='EST5EDT,M3.2.0/2,M11.1.0/2'>US/Eastern</option>
      <option value='CST6CDT,M3.2.0/2,M11.1.0/2'>US/Central</option>
      <option value='MST7MDT,M3.2.0/2,M11.1.0/2'>US/Mountain</option>
      <option value='PST8PDT,M3.2.0/2,M11.1.0/2'>US/Pacific</option>
      <option value='AKST9AKDT,M3.2.0/2,M11.1.0/2'>US/Alaska</option>
      <option value='HST10'>US/Hawaii</option>
      <option value='GMT0'>GMT</option>
    </select>
    <script>
      document.addEventListener('DOMContentLoaded', function(){
        var sel = document.getElementById('tzsel');
        var hid = document.getElementById('tz') || document.getElementsByName('tz')[0];
        if(hid && sel){
          for (var i=0;i<sel.options.length;i++){
            if (sel.options[i].value === hid.value){ sel.selectedIndex=i; break; }
          }
          function sync(){ hid.value = sel.value; }
          sync();
          sel.addEventListener('change', sync);
        }
      });
    </script>
    <br/><br/>
  )rawliteral";
  WiFiManagerParameter tzDropdown(tz_html.c_str());
  wm.addParameter(&tzDropdown);

  // ---- Special Days (1-5) ----
  const char* monthNames[] = {
    "January","February","March","April","May","June",
    "July","August","September","October","November","December"
  };
  // One hidden CSV field per day: "month,day,mode,RRGGBB,RRGGBB,RRGGBB"
  String sd1def = serializeSpecial(special[0]);
  String sd2def = serializeSpecial(special[1]);
  String sd3def = serializeSpecial(special[2]);
  String sd4def = serializeSpecial(special[3]);
  String sd5def = serializeSpecial(special[4]);
  WiFiManagerParameter sd1Param("sd1", "", sd1def.c_str(), 48, "type='hidden'"); wm.addParameter(&sd1Param);
  WiFiManagerParameter sd2Param("sd2", "", sd2def.c_str(), 48, "type='hidden'"); wm.addParameter(&sd2Param);
  WiFiManagerParameter sd3Param("sd3", "", sd3def.c_str(), 48, "type='hidden'"); wm.addParameter(&sd3Param);
  WiFiManagerParameter sd4Param("sd4", "", sd4def.c_str(), 48, "type='hidden'"); wm.addParameter(&sd4Param);
  WiFiManagerParameter sd5Param("sd5", "", sd5def.c_str(), 48, "type='hidden'"); wm.addParameter(&sd5Param);
  WiFiManagerParameter* sdP[NUM_SPECIAL] = { &sd1Param, &sd2Param, &sd3Param, &sd4Param, &sd5Param };

  String sdHtml;
  sdHtml.reserve(8000);
  sdHtml += F("<label>Special Days:</label><br/>"
              "<small>Pick a date, then 3 Colors (hour / minute / connecting word) or Rainbow. "
              "Set month to —— to turn a day off.</small><br/><br/>");
  for (int i = 0; i < NUM_SPECIAL; i++) {
    SpecialDay& sd = special[i];
    sdHtml += "<div style='border:1px solid #ccc;border-radius:6px;padding:8px;margin-bottom:8px'>";
    sdHtml += "<b>Special Day " + String(i + 1) + "</b><br/>";
    sdHtml += "<select id='m" + String(i) + "' style='width:48%;padding:6px;margin-right:4%'>";
    sdHtml += "<option value='0'>—— (off)</option>";
    for (int m = 1; m <= 12; m++) sdHtml += "<option value='" + String(m) + "'>" + monthNames[m-1] + "</option>";
    sdHtml += "</select>";
    sdHtml += "<select id='d" + String(i) + "' style='width:48%;padding:6px'>";
    for (int d = 1; d <= 31; d++) sdHtml += "<option value='" + String(d) + "'>" + String(d) + "</option>";
    sdHtml += "</select><br/>";
    sdHtml += "<select id='md" + String(i) + "' style='width:48%;padding:6px;margin-top:6px;margin-right:4%'>";
    sdHtml += "<option value='0'>3 Colors</option><option value='1'>Rainbow</option></select>";
    char cc[3][8];
    snprintf(cc[0], 8, "#%06lX", (unsigned long)(sd.c1 & 0xFFFFFF));
    snprintf(cc[1], 8, "#%06lX", (unsigned long)(sd.c2 & 0xFFFFFF));
    snprintf(cc[2], 8, "#%06lX", (unsigned long)(sd.c3 & 0xFFFFFF));
    sdHtml += "<div id='cw" + String(i) + "' style='margin-top:6px'>";
    sdHtml += "<small>3-color mode (ignored for Rainbow):</small><br/>";
    sdHtml += "<label style='font-size:85%'>Hour <input type='color' id='c1_" + String(i) + "' value='" + cc[0] + "'></label> ";
    sdHtml += "<label style='font-size:85%'>Minute <input type='color' id='c2_" + String(i) + "' value='" + cc[1] + "'></label> ";
    sdHtml += "<label style='font-size:85%'>Word <input type='color' id='c3_" + String(i) + "' value='" + cc[2] + "'></label>";
    sdHtml += "</div></div>";
  }
  sdHtml += "<script>var SD=[";
  for (int i = 0; i < NUM_SPECIAL; i++)
    sdHtml += "[" + String(special[i].month) + "," + String(special[i].day) + "," + String(special[i].mode) + "],";
  sdHtml += "];\n";
  sdHtml += F(R"rawliteral(
    document.addEventListener('DOMContentLoaded', function(){
      for (var i=0;i<5;i++) (function(i){
        var m=document.getElementById('m'+i), d=document.getElementById('d'+i), md=document.getElementById('md'+i);
        var cw=document.getElementById('cw'+i), hid=document.getElementsByName('sd'+(i+1))[0];
        m.value=SD[i][0]; d.value=SD[i][1]; md.value=SD[i][2];
        function hx(id){return document.getElementById(id).value.replace('#','').toUpperCase();}
        function dim(){ cw.style.opacity=(md.value==='1')?'0.4':'1'; }
        function upd(){ hid.value=m.value+','+d.value+','+md.value+','+hx('c1_'+i)+','+hx('c2_'+i)+','+hx('c3_'+i); }
        [m,d,md].forEach(function(e){e.addEventListener('change',function(){dim();upd();});});
        ['c1_','c2_','c3_'].forEach(function(p){document.getElementById(p+i).addEventListener('input',upd);});
        dim(); upd();
      })(i);
    });
  )rawliteral");
  sdHtml += "</script><br/>";
  WiFiManagerParameter sdHTMLParam(sdHtml.c_str());
  wm.addParameter(&sdHTMLParam);

  // Layout
  WiFiManagerParameter layoutParam("layout", "", String(layout).c_str(), 4, "type='hidden'");
  wm.addParameter(&layoutParam);

  String layoutHtml;
  layoutHtml.reserve(600);
  layoutHtml += F("<label>Clock Layout:</label><br/>");
  layoutHtml += F("<select id='layout_sel' style='width:100%;padding:6px;'>");
  layoutHtml += F("<option value='0'>Vertical (4x36)</option>");
  layoutHtml += F("<option value='1'>Horizontal (36x4)</option>");
  layoutHtml += F("</select>");
  layoutHtml += F(R"rawliteral(
    <script>
      document.addEventListener('DOMContentLoaded', function(){
        var sel = document.getElementById('layout_sel');
        var hid = document.getElementsByName('layout')[0];
        if(sel && hid){
          for(var i=0;i<sel.options.length;i++){
            if(sel.options[i].value===hid.value){ sel.selectedIndex=i; break; }
          }
          sel.addEventListener('change', function(){ hid.value=this.value; });
        }
      });
    </script>
    <br/><br/>
  )rawliteral");
  WiFiManagerParameter layoutHTMLParam(layoutHtml.c_str());
  wm.addParameter(&layoutHTMLParam);

#ifdef LED_PIN_SELECTABLE
  // LED data pin (classic ESP32 only). Changing it reboots the clock to apply.
  WiFiManagerParameter ledPinParam("ledpin", "", String(ledPin).c_str(), 4, "type='hidden'");
  wm.addParameter(&ledPinParam);

  String pinHtml;
  pinHtml.reserve(800);
  pinHtml += F("<label>LED Data Pin (GPIO):</label><br/>");
  pinHtml += F("<select id='ledpin_sel' style='width:100%;padding:6px;'>");
  for (uint8_t c : LED_PIN_CHOICES) {
    pinHtml += "<option value='" + String(c) + "'>GPIO " + String(c) + (c == DEFAULT_LED_PIN ? " (default)" : "") + "</option>";
  }
  pinHtml += F("</select>");
  pinHtml += F("<small style='color:#888'>Only change this if the LEDs stay dark. The clock restarts after saving.</small>");
  pinHtml += F(R"rawliteral(
    <script>
      document.addEventListener('DOMContentLoaded', function(){
        var sel = document.getElementById('ledpin_sel');
        var hid = document.getElementsByName('ledpin')[0];
        if(sel && hid){
          for(var i=0;i<sel.options.length;i++){
            if(sel.options[i].value===hid.value){ sel.selectedIndex=i; break; }
          }
          sel.addEventListener('change', function(){ hid.value=this.value; });
        }
      });
    </script>
    <br/><br/>
  )rawliteral");
  WiFiManagerParameter ledPinHTMLParam(pinHtml.c_str());
  wm.addParameter(&ledPinHTMLParam);
#endif

  // Firmware version label (visible in the config portal)
  String fwHtml = String("<p style='text-align:center;color:#888;margin-top:8px'>Firmware v") + FW_VERSION + " (" BOARD_NAME ")</p>";
  WiFiManagerParameter fwLabel(fwHtml.c_str());
  wm.addParameter(&fwLabel);

  // Save callback
  wm.setSaveParamsCallback([&](){
    const char* tz = tzParam.getValue();

    int lTmp = atoi(layoutParam.getValue());
    if (lTmp < 0 || lTmp > 1) lTmp = 0;

    prefs.begin("settings", false);
    prefs.putString("tz",        tz);
    for (int i = 0; i < NUM_SPECIAL; i++) {
      SpecialDay tmp = special[i];
      parseSpecial(String(sdP[i]->getValue()), tmp);
      special[i] = tmp;
      String key = "sd" + String(i + 1);
      prefs.putString(key.c_str(), serializeSpecial(tmp));
    }
    prefs.putUChar("layout",     (uint8_t)lTmp);
#ifdef LED_PIN_SELECTABLE
    int pTmp = atoi(ledPinParam.getValue());
    if (!validLedPin(pTmp)) pTmp = ledPin;
    if (pTmp != ledPin) {
      prefs.putUChar("ledPin", (uint8_t)pTmp);
      pinChangePending = true;
      Serial.printf("[PORTAL]   LED pin : GPIO%d -> GPIO%d (reboot pending)\n", ledPin, pTmp);
    }
#endif
    prefs.end();

    layout    = (uint8_t)lTmp;
    applyLayout();

    Serial.println("[PORTAL] Settings saved:");
    Serial.printf("[PORTAL]   TZ      : %s\n", tz);
    for (int i = 0; i < NUM_SPECIAL; i++)
      Serial.printf("[PORTAL]   SpecialDay %d: %02u/%02u mode=%u\n", i+1, special[i].month, special[i].day, special[i].mode);
    Serial.printf("[PORTAL]   Layout  : %s\n", layout == 0 ? "Vertical" : "Horizontal");
  });

  wm.setConfigPortalBlocking(true);
  wm.setConnectTimeout(12);
  wm.setConfigPortalTimeout(240);

  bool connected = false;

  if (!forcePortalAtBoot) {
    Serial.println("[WIFI] Trying stored credentials...");
    WiFi.begin();
    unsigned long t0 = millis();
    int dots = 0;
    while ((millis() - t0) < 12000 && WiFi.status() != WL_CONNECTED) {
      if (dots++ % 10 == 0) Serial.print(".");
      delay(100);
    }
    Serial.println();
    connected = (WiFi.status() == WL_CONNECTED);
    if (connected) {
      Serial.printf("[WIFI] Connected! SSID: %s  IP: %s\n",
        WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    } else {
      Serial.println("[WIFI] Stored credentials failed or none saved.");
    }
  }

  if (!connected) {
    Serial.println("[WIFI] Opening captive portal: \"Word Clock Setup\"");
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_AP_STA);
    delay(100);
    if (!wm.autoConnect("Word Clock Setup")) {
      Serial.println("[WIFI] Portal timed out without connection. Rebooting in 3s...");
      delay(3000);
      ESP.restart();
    }
    connected = (WiFi.status() == WL_CONNECTED);
    if (connected) {
      Serial.printf("[WIFI] Connected via portal! SSID: %s  IP: %s\n",
        WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    }
  }

  if (!connected) {
    Serial.println("[WIFI] Still not connected -- forcing restart.");
    delay(1000);
    ESP.restart();
  }

  if (pinChangePending) {
    Serial.println("[LED] LED pin changed in portal -- restarting to apply.");
    delay(500);
    ESP.restart();
  }

  WIFI_OK = true;

  // Reload prefs post-connect
  Serial.println("[PREFS] Reloading settings post-connect...");
  prefs.begin("settings", true);
  String timezone = prefs.getString("tz", "EST5EDT,M3.2.0/2,M11.1.0/2");
  layout    = prefs.getUChar("layout",    layout);
  loadSpecialDays();
  prefs.end();

  if (layout > 1) layout = 0;

  applyLayout();

  Serial.printf("[PREFS] TZ: %s  SpecialDay1: %02u/%02u  Layout: %s  Brightness: %u (fixed)\n",
    timezone.c_str(), special[0].month, special[0].day,
    layout == 0 ? "Vertical" : "Horizontal", brightness);

  applyTimezone(timezone.c_str());
  ensureTimeSynced();

  nowEpoch = time(nullptr);
  localtime_r(&nowEpoch, &tm_now);
  year_   = tm_now.tm_year + 1900;
  month_  = tm_now.tm_mon + 1;
  day_    = tm_now.tm_mday;
  hour_   = tm_now.tm_hour;
  minute_ = tm_now.tm_min;

  Serial.printf("[CLOCK] Initial display: %02d/%02d/%04d %02d:%02d\n",
    month_, day_, year_, hour_, minute_);
  transitionToTime(hour_, minute_);   // initial draw (fades up from black)

  checkForUpdate("boot");             // OTA check on startup (reboots if it updates)

  Serial.println("[BOOT] Setup complete. Clock running.");
  Serial.println("========================================\n");
}

// ============================================================================
// loop()
// ============================================================================
void loop() {
  static int lastMinute = -1;

  static uint32_t wifiLostAt = 0;
  if (!WIFI_OK) {
    if (wifiLostAt == 0) {
      wifiLostAt = millis();
      Serial.println("[WIFI] Connection lost -- watchdog started (60s to restart).");
    }
    if (millis() - wifiLostAt > 60000) {
      Serial.println("[WIFI] Did not recover in 60s -- restarting.");
      ESP.restart();
    }
  } else {
    if (wifiLostAt != 0) {
      Serial.println("[WIFI] Reconnected successfully.");
      wifiLostAt = 0;
    }
  }

  static uint32_t lastBtnCheck = 0;
  uint32_t nowMs = millis();
  if (nowMs - lastBtnCheck > 100) {
    lastBtnCheck = nowMs;
    if (digitalRead(BTN_PIN) == LOW) {
      Serial.println("[BTN] Button pressed -- reopening portal.");
      WiFiManager wm;
      wm.setConfigPortalBlocking(true);
      if (!wm.autoConnect("Word Clock Setup")) {
        Serial.println("[BTN] Portal closed without connection -- keeping prior Wi-Fi.");
      } else {
        Serial.println("[BTN] Portal saved new settings.");
        prefs.begin("settings", true);
        String timezone = prefs.getString("tz", "EST5EDT,M3.2.0/2,M11.1.0/2");
        layout    = prefs.getUChar("layout",    layout);
        loadSpecialDays();
        prefs.end();
        if (layout > 1) layout = 0;
        applyLayout();
        Serial.printf("[BTN] Applied TZ: %s  SpecialDay1: %02u/%02u  Layout: %s\n",
          timezone.c_str(), special[0].month, special[0].day,
          layout == 0 ? "Vertical" : "Horizontal");
        applyTimezone(timezone.c_str());
        ensureTimeSynced();
        lastMinute = -1;
      }
    }
  }

  nowEpoch = time(nullptr);
  localtime_r(&nowEpoch, &tm_now);

  if (tm_now.tm_min != lastMinute) {
    lastMinute = tm_now.tm_min;
    year_   = tm_now.tm_year + 1900;
    month_  = tm_now.tm_mon + 1;
    day_    = tm_now.tm_mday;
    hour_   = tm_now.tm_hour;
    minute_ = tm_now.tm_min;

    // Daily OTA check at ~4am local (once per calendar day)
    if (hour_ == 4 && tm_now.tm_yday != otaLastCheckYday) {
      otaLastCheckYday = tm_now.tm_yday;
      checkForUpdate("daily");
    }

    int dispHour = hour_;
    int dispMin  = minute_;
    g_forcePalette = 0; g_forceRainbow = false; g_forceJuly4 = false;

    if (isAprilFools()) {
      dispHour = random(0, 24);
      dispMin  = random(0, 60);
      uint8_t pick = random(0, 11);
      switch (pick) {
        case 0: g_forcePalette = 1; break;
        case 1: g_forcePalette = 2; break;
        case 2: g_forcePalette = 3; break;
        case 3: g_forcePalette = 4; break;
        case 4: g_forcePalette = 5; break;
        case 5: g_forcePalette = 6; break;
        case 6: g_forcePalette = 7; break;
        case 7: g_forcePalette = 8; break;
        case 8: g_forcePalette = 9; break;
        case 9: g_forceRainbow = true; break;
        case 10:g_forceJuly4   = true; break;
      }
      Serial.printf("[APRILFOOLS] real=%02d:%02d disp=%02d:%02d palette=%u rb=%d j4=%d\n",
        hour_, minute_, dispHour, dispMin,
        g_forcePalette, (int)g_forceRainbow, (int)g_forceJuly4);
    }

    Serial.printf("[CLOCK] %02d/%02d/%04d  real=%02d:%02d  disp=%02d:%02d  palette=%u rainbow=%d july4=%d\n",
      month_, day_, year_, hour_, minute_, dispHour, dispMin,
      g_forcePalette, (int)g_forceRainbow, (int)g_forceJuly4);

    transitionToTime(dispHour, dispMin);   // smooth staggered fade (replaces clear/showTime/power-limit)

    Serial.printf("[CLOCK] Frame done. LEDs lit: %u  Brightness: %u\n",
      frameLitCount, pixels.getBrightness());
  }

  delay(200);
}

// ============================================================================
// Time helpers
// ============================================================================
void applyTimezone(const char* tz) {
  if (!tz || !*tz) tz = "EST5EDT,M3.2.0/2,M11.1.0/2";
  setenv("TZ", tz, 1);
  tzset();
  configTzTime(tz, ntpServerA, ntpServerB, ntpServerC);
  Serial.printf("[TZ] Applied: %s\n", tz);
}

void ensureTimeSynced() {
  Serial.println("[NTP] Waiting for time sync...");
  struct tm ti{};
  int attempts = 0;
  while (!getLocalTime(&ti, 1000) && attempts < 20) {
    Serial.printf("[NTP] Attempt %d/20...\n", attempts + 1);
    int idx = wifiAnim[attempts % LEN_wifiAnim];
    pixels.clear();
    if (idx >= 0 && idx < NUMPIXELS) {
      pixels.setPixelColor(idx, pixels.Color(40, 40, 40));
    }
    pixels.show();
    attempts++;
  }
  pixels.clear();
  pixels.show();
  if (attempts < 20) {
    Serial.printf("[NTP] Synced after %d attempt(s): %02d:%02d:%02d on %02d/%02d/%04d\n",
      attempts + 1, ti.tm_hour, ti.tm_min, ti.tm_sec,
      ti.tm_mon + 1, ti.tm_mday, ti.tm_year + 1900);
  } else {
    Serial.println("[NTP] WARNING: Sync timed out after 20 attempts. Time may be wrong.");
  }
}

// ============================================================================
// Colors
// ============================================================================
void setColor(int order) {
  g_curGroup = (order==1) ? 1 : (order==2) ? 2 : (order==3) ? 3 : 0;  // tag pixels for staggered fade
  if (g_forcePalette) {
    switch (g_forcePalette) {
      case 1: switch(order){ case 1: r=240; g= 90; b=110; break; case 2: r=245; g=185; b= 60; break; case 3: r=255; g=235; b=140; break; default: r=g=b=150; } return;
      case 2: switch(order){ case 1: r=120; g=210; b=230; break; case 2: r= 30; g=150; b= 85; break; case 3: r=255; g=190; b=220; break; default: r=g=b=150; } return;
      case 3: switch(order){ case 1: r=220; g=110; b= 40; break; case 2: r=180; g= 60; b= 30; break; case 3: r=235; g=200; b=120; break; default: r=g=b=150; } return;
      case 4: switch(order){ case 1: r=160; g=210; b=255; break; case 2: r=100; g=140; b=170; break; case 3: r= 40; g= 70; b=140; break; default: r=g=b=150; } return;
      case 5: switch(order){ case 1: r=220; g= 40; b= 80; break; case 2: r=255; g=160; b=200; break; case 3: r=255; g=240; b=245; break; default: r=g=b=150; } return;
      case 6: switch(order){ case 1: r= 20; g=120; b= 60; break; case 2: r= 80; g=170; b= 90; break; case 3: r=230; g=170; b= 40; break; default: r=g=b=150; } return;
      case 7: switch(order){ case 1: r=255; g=120; b=  0; break; case 2: r=110; g= 60; b=150; break; case 3: r=255; g=255; b=255; break; default: r=g=b=150; } return;
      case 8: switch(order){ case 1: r=220; g=110; b= 40; break; case 2: r=140; g= 80; b= 30; break; case 3: r=235; g=200; b=120; break; default: r=g=b=150; } return;
      case 9: switch(order){ case 1: r=200; g= 30; b= 30; break; case 2: r= 20; g=120; b= 60; break; case 3: r=255; g=230; b=120; break; default: r=g=b=150; } return;
    }
  }
  // Special day (highest priority after the April-Fools gag), 3-color mode
  if (g_activeSpecial >= 0 && special[g_activeSpecial].mode == SD_COLORS) {
    uint32_t c = (order == 1) ? special[g_activeSpecial].c1
               : (order == 2) ? special[g_activeSpecial].c2
               : (order == 3) ? special[g_activeSpecial].c3 : 0x969696;
    uint8_t rr, gg, bb; unpack(c, rr, gg, bb);
    r = rr; g = gg; b = bb; return;
  }
  if (isValentine())   { switch(order){ case 1: r=220; g= 40; b= 80; break; case 2: r=255; g=160; b=200; break; case 3: r=255; g=240; b=245; break; default: r=g=b=150; } return; }
  if (isStPatrick())   { switch(order){ case 1: r= 20; g=120; b= 60; break; case 2: r= 80; g=170; b= 90; break; case 3: r=230; g=170; b= 40; break; default: r=g=b=150; } return; }
  if (isHalloween())   { switch(order){ case 1: r=255; g=120; b=  0; break; case 2: r=110; g= 60; b=150; break; case 3: r= 80; g= 80; b= 80; break; default: r=g=b=150; } return; }
  if (isThanksgiving()){ switch(order){ case 1: r=220; g=110; b= 40; break; case 2: r=140; g= 80; b= 30; break; case 3: r=235; g=200; b=120; break; default: r=g=b=150; } return; }
  if (isChristmas())   { switch(order){ case 1: r=200; g= 30; b= 30; break; case 2: r= 20; g=120; b= 60; break; case 3: r=255; g=230; b=120; break; default: r=g=b=150; } return; }

  if (month_==3||month_==4||month_==5) {
    switch(order){ case 1: r=240; g= 90; b=110; break; case 2: r=245; g=185; b= 60; break; case 3: r=255; g=235; b=140; break; default: r=150; g=150; b=150; }
  } else if (month_==6||month_==7||month_==8||(month_==9&&day_<22)) {
    switch(order){ case 1: r=120; g=210; b=230; break; case 2: r= 30; g=150; b= 85; break; case 3: r=255; g=190; b=220; break; default: r=150; g=150; b=150; }
  } else if ((month_==9&&day_>=22)||month_==10||month_==11||(month_==12&&day_<22)) {
    switch(order){ case 1: r=220; g=110; b= 40; break; case 2: r=180; g= 60; b= 30; break; case 3: r=235; g=200; b=120; break; default: r=150; g=150; b=150; }
  } else {
    switch(order){ case 1: r=160; g=210; b=255; break; case 2: r=100; g=140; b=170; break; case 3: r= 40; g= 70; b=140; break; default: r=150; g=150; b=150; }
  }
}

// ============================================================================
// showTime()
// ============================================================================
void showTime(int hour, int minute) {
  frameLitCount = 0;
  g_activeSpecial = activeSpecialDay();

  setColor(2);
  switch (minute) {
    case 0:  if (hour != 0 && hour != 12) setLEDs(oclock, LEN_oclock); break;
    case 1:  case 59: setLEDs(oneMin,    LEN_oneMin);   break;
    case 2:  case 58: setLEDs(twoMin,    LEN_twoMin);   break;
    case 3:  case 57: setLEDs(threeMin,  LEN_threeMin); break;
    case 4:  case 56: setLEDs(fourMin,   LEN_fourMin);  break;
    case 5:  case 55: setLEDs(fiveMin,   LEN_fiveMin);  break;
    case 6:  case 54: setLEDs(sixMin,    LEN_sixMin);   break;
    case 7:  case 53: setLEDs(sevenMin,  LEN_sevenMin); break;
    case 8:  case 52: setLEDs(eightMin,  LEN_eightMin); break;
    case 9:  case 51: setLEDs(nineMin,   LEN_nineMin);  break;
    case 10: case 50: setLEDs(tenMin,    LEN_tenMin);   break;
    case 11: case 49: setLEDs(elevenMin, LEN_elevenMin); break;
    case 12: case 48: setLEDs(twelveMin, LEN_twelveMin); break;
    case 13: case 47: setLEDs(thirteen,  LEN_thirteen); break;
    case 14: case 46: setLEDs(fourteen,  LEN_fourteen); break;
    case 15: case 45: if(random(0,2)==0) setLEDs(quarter,LEN_quarter); else setLEDs(fifteen,LEN_fifteen); break;
    case 16: case 44: setLEDs(sixteen,   LEN_sixteen);  break;
    case 17: case 43: setLEDs(seventeen, LEN_seventeen); break;
    case 18: case 42: setLEDs(eighteen,  LEN_eighteen); break;
    case 19: case 41: setLEDs(nineteen,  LEN_nineteen); break;
    case 20: case 40: setLEDs(twenty,    LEN_twenty);   break;
    case 21: case 39: setLEDs(twenty,LEN_twenty); setLEDs(oneMin,LEN_oneMin);     break;
    case 22: case 38: setLEDs(twenty,LEN_twenty); setLEDs(twoMin,LEN_twoMin);     break;
    case 23: case 37: setLEDs(twenty,LEN_twenty); setLEDs(threeMin,LEN_threeMin); break;
    case 24: case 36: setLEDs(twenty,LEN_twenty); setLEDs(fourMin,LEN_fourMin);   break;
    case 25: case 35: setLEDs(twenty,LEN_twenty); setLEDs(fiveMin,LEN_fiveMin);   break;
    case 26: case 34: setLEDs(twenty,LEN_twenty); setLEDs(sixMin,LEN_sixMin);     break;
    case 27: case 33: setLEDs(twenty,LEN_twenty); setLEDs(sevenMin,LEN_sevenMin); break;
    case 28: case 32: setLEDs(twenty,LEN_twenty); setLEDs(eightMin,LEN_eightMin); break;
    case 29: case 31: setLEDs(twenty,LEN_twenty); setLEDs(nineMin,LEN_nineMin);   break;
    case 30: setLEDs(half_, LEN_half_); break;
    default: setLEDs(half_, LEN_half_); break;
  }

  setColor(3);
  int hourForDisplay = hour;

  if (minute > 0 && minute <= 30) {
    if (random(0,2)==0) setLEDs(after,LEN_after); else setLEDs(past,LEN_past);
  } else if (minute > 30) {
    if (minute==59||minute==47||minute==46) {
      int pick=random(0,3);
      if(pick==0) setLEDs(of,LEN_of); else if(pick==1) setLEDs(til,LEN_til); else setLEDs(before,LEN_before);
    } else {
      int pick=random(0,3);
      if(pick==0) setLEDs(of,LEN_of); else if(pick==1) setLEDs(til,LEN_til); else setLEDs(to_,LEN_to_);
    }
    hourForDisplay = (hour + 1) % 24;
  }

  setColor(1);
  switch (hourForDisplay) {
    case 0:  if(minute==0) setLEDs(midnight,LEN_midnight); else { if(random(0,2)==0) setLEDs(twelveHour,LEN_twelveHour); else setLEDs(midnight,LEN_midnight); } break;
    case 12: if(minute==0) setLEDs(noon,LEN_noon);         else { if(random(0,2)==0) setLEDs(twelveHour,LEN_twelveHour); else setLEDs(noon,LEN_noon); }         break;
    case 1:  case 13: if(minute==0) setLEDs(oneMin,LEN_oneMin);       else setLEDs(oneHour,LEN_oneHour);       break;
    case 2:  case 14: if(minute==0) setLEDs(twoMin,LEN_twoMin);       else setLEDs(twoHour,LEN_twoHour);       break;
    case 3:  case 15: if(minute==0) setLEDs(threeMin,LEN_threeMin);   else setLEDs(threeHour,LEN_threeHour);   break;
    case 4:  case 16: if(minute==0) setLEDs(fourMin,LEN_fourMin);     else setLEDs(fourHour,LEN_fourHour);     break;
    case 5:  case 17: if(minute==0) setLEDs(fiveMin,LEN_fiveMin);     else setLEDs(fiveHour,LEN_fiveHour);     break;
    case 6:  case 18: if(minute==0) setLEDs(sixMin,LEN_sixMin);       else setLEDs(sixHour,LEN_sixHour);       break;
    case 7:  case 19: if(minute==0) setLEDs(sevenMin,LEN_sevenMin);   else setLEDs(sevenHour,LEN_sevenHour);   break;
    case 8:  case 20: if(minute==0) setLEDs(eightMin,LEN_eightMin);   else setLEDs(eightHour,LEN_eightHour);   break;
    case 9:  case 21: if(minute==0) setLEDs(nineMin,LEN_nineMin);     else setLEDs(nineHour,LEN_nineHour);     break;
    case 10: case 22: if(minute==0) setLEDs(tenMin,LEN_tenMin);       else setLEDs(tenHour,LEN_tenHour);       break;
    case 11: case 23: if(minute==0) setLEDs(elevenMin,LEN_elevenMin); else setLEDs(elevenHour,LEN_elevenHour); break;
    default: if(minute!=0) setLEDs(twelveHour,LEN_twelveHour); break;
  }
}

// ============================================================================
// Gamma + LED writer
// ============================================================================
uint8_t gamma8(uint8_t v){
  return (uint8_t)(powf(v/255.0f, 2.2f) * 255.0f + 0.5f);
}

void setLEDs(int a[], int len) {
  const bool sActive  = (g_activeSpecial >= 0);
  const bool sRainbow = sActive && special[g_activeSpecial].mode == SD_RAINBOW;
  const bool july4   = g_forceJuly4   || (!sActive && isJuly4());
  const bool rainbow = g_forceRainbow || sRainbow || (!sActive && isPride());
  uint8_t baseR=gamma8(r), baseG=gamma8(g), baseB=gamma8(b);

  for (int i=0; i<len; i++) {
    int idx = a[i];
    if (idx<0||idx>=NUMPIXELS) continue;
    uint8_t rr=baseR, gg=baseG, bb=baseB;
    if (rainbow) {
      if (RAINBOW_PER_LETTER) {
        uint8_t pos=(uint8_t)((i*256)/(len>1?(len-1):1));
        uint32_t c=wheel(pos); unpack(c,rr,gg,bb);
        rr=gamma8(rr); gg=gamma8(gg); bb=gamma8(bb);
      } else {
        uint8_t pos=(uint8_t)((a[0]*97)&0xFF);
        uint32_t c=wheel(pos); unpack(c,rr,gg,bb);
        rr=gamma8(rr); gg=gamma8(gg); bb=gamma8(bb);
      }
    } else if (july4) {
      if (JULY4_PER_LETTER) {
        switch(i%3){
          case 0: rr=gamma8(255); gg=0;           bb=0;           break;
          case 1: rr=gamma8(255); gg=gamma8(255); bb=gamma8(255); break;
          default: rr=0;          gg=0;            bb=gamma8(255); break;
        }
      } else {
        rr=0; gg=0; bb=gamma8(255);
      }
    }
    tgt[idx][0]=rr; tgt[idx][1]=gg; tgt[idx][2]=bb;   // write into target frame, not the strip
    grp[idx]=g_curGroup;
    if (rr||gg||bb) frameLitCount++;
  }
}

// ============================================================================
// Power limiter
// ============================================================================
void applyPowerLimitAndShow() {
  uint8_t original = brightness;
  if (frameLitCount>90 && original>80) pixels.setBrightness(80);
  else if (frameLitCount>60 && original>70) pixels.setBrightness(70);
  pixels.show();
  if (pixels.getBrightness()!=original) pixels.setBrightness(original);
}

// ============================================================================
// Smooth staggered transition: outgoing -> black, then connector, minute, hour.
// Unchanged words (e.g. the hour when it hasn't rolled over) are left lit.
// ============================================================================
static uint8_t BLACK[NUMPIXELS][3];      // stays all-zero; fade-out target
static bool mOut[NUMPIXELS], mConn[NUMPIXELS], mMin[NUMPIXELS], mHour[NUMPIXELS];

static inline void pushLive() {
  for (int i=0;i<NUMPIXELS;i++)
    pixels.setPixelColor(i, pixels.Color(live[i][0], live[i][1], live[i][2]));
  pixels.show();
}

static inline bool anySet(const bool m[]) {
  for (int i=0;i<NUMPIXELS;i++) if (m[i]) return true;
  return false;
}

// Fade pixels flagged in mask[] from their current live color toward dst[][3].
void fadeMasked(const bool mask[], const uint8_t dst[][3], uint16_t ms) {
  static uint8_t startc[NUMPIXELS][3];
  memcpy(startc, live, sizeof(startc));
  for (int s=1; s<=FADE_STEPS; s++) {
    float t = (float)s / FADE_STEPS;
    for (int i=0;i<NUMPIXELS;i++) {
      if (mask[i]) {
        for (int k=0;k<3;k++) {
          int a=startc[i][k], z=dst[i][k];
          live[i][k] = (uint8_t)(a + (int)((z-a)*t + (z>=a?0.5f:-0.5f)));
        }
      }
    }
    pushLive();
    delay(ms / FADE_STEPS);
  }
  for (int i=0;i<NUMPIXELS;i++) if (mask[i])
    for (int k=0;k<3;k++) live[i][k]=dst[i][k];   // snap to exact target
}

void transitionToTime(int h, int m) {
  memset(tgt, 0, sizeof(tgt));
  memset(grp, 0, sizeof(grp));
  frameLitCount = 0;
  showTime(h, m);                         // fills tgt[], grp[], frameLitCount

  for (int i=0;i<NUMPIXELS;i++) {
    bool oldLit = live[i][0]||live[i][1]||live[i][2];
    bool newLit = tgt[i][0]||tgt[i][1]||tgt[i][2];
    bool same   = newLit && oldLit &&
                  live[i][0]==tgt[i][0] && live[i][1]==tgt[i][1] && live[i][2]==tgt[i][2];
    mOut[i]  = oldLit && !same;            // fade out (hour included only if it changed)
    bool in  = newLit && !same;
    mConn[i] = in && grp[i]==3;
    mMin[i]  = in && grp[i]==2;
    mHour[i] = in && grp[i]==1;
    // 'same' pixels are left untouched -> they stay lit through the transition
  }

  uint8_t original = brightness;
  if      (frameLitCount>90 && original>80) pixels.setBrightness(80);
  else if (frameLitCount>60 && original>70) pixels.setBrightness(70);

  if (anySet(mOut)) fadeMasked(mOut, BLACK, FADE_OUT_MS);    // step 1: outgoing -> black
  if (m == 0) {
    // top of the hour ("TEN O'CLOCK"): HOUR first, then O'CLOCK
    if (anySet(mHour)) fadeMasked(mHour, tgt, FADE_IN_MS);
    if (anySet(mMin))  fadeMasked(mMin,  tgt, FADE_IN_MS);   // "O'CLOCK" (or noon/midnight remainder)
    if (anySet(mConn)) fadeMasked(mConn, tgt, FADE_IN_MS);   // (no connector at :00)
  } else {
    if (anySet(mMin))  fadeMasked(mMin,  tgt, FADE_IN_MS);   // step 2: minute fades in
    if (anySet(mConn)) fadeMasked(mConn, tgt, FADE_IN_MS);   // step 3: connector fades in
    if (anySet(mHour)) fadeMasked(mHour, tgt, FADE_IN_MS);   // step 4: hour fades in (if changed)
  }

  memcpy(live, tgt, sizeof(live));
  pushLive();
  if (pixels.getBrightness()!=original) pixels.setBrightness(original);
}

// ============================================================================
// OTA: read version.json; if a newer version is published, download & flash.
// ============================================================================
void checkForUpdate(const char* reason) {
  if (!WIFI_OK) { Serial.println("[OTA] skip (no WiFi)"); return; }
  Serial.printf("[OTA] Checking (%s). Running FW_VERSION=%d\n", reason, FW_VERSION);

  WiFiClientSecure client;
  client.setInsecure();                         // public repo; no cert pinning needed
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  if (!http.begin(client, OTA_VERSION_URL)) { Serial.println("[OTA] begin() failed"); return; }
  int code = http.GET();
  if (code != HTTP_CODE_OK) { Serial.printf("[OTA] version.json HTTP %d\n", code); http.end(); return; }
  String payload = http.getString();
  http.end();

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) { Serial.printf("[OTA] JSON error: %s\n", err.c_str()); return; }
  int latest = doc["version"] | -1;
  Serial.printf("[OTA] published=%d running=%d\n", latest, FW_VERSION);
  if (latest <= FW_VERSION) { Serial.println("[OTA] Up to date."); return; }

  Serial.printf("[OTA] Updating %d -> %d\n", FW_VERSION, latest);
  WiFiClientSecure upClient;
  upClient.setInsecure();
  httpUpdate.rebootOnUpdate(true);
  httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  t_httpUpdate_return ret = httpUpdate.update(upClient, OTA_FIRMWARE_URL);
  if (ret == HTTP_UPDATE_FAILED)
    Serial.printf("[OTA] FAILED (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
  else if (ret == HTTP_UPDATE_NO_UPDATES)
    Serial.println("[OTA] No update at firmware URL.");
  // HTTP_UPDATE_OK reboots automatically.
}

// ============================================================================
// Portal animation
// ============================================================================
void animateIndices(const int* seq, int len, uint16_t hold_ms) {
  for (int i=0; i<len; ++i) {
    pixels.clear();
    int idx=seq[i];
    if (idx>=0&&idx<NUMPIXELS) pixels.setPixelColor(idx, pixels.Color(40,40,40));
    pixels.show();
    delay(hold_ms);
  }
}

void runPortalAnimation(WiFiManager& wm) {
  while (wm.getConfigPortalActive()) {
    wm.process();
    animateIndices(wifiAnim, LEN_wifiAnim, 180);
    wm.process();
    animateIndices(noAnim,   LEN_noAnim,   220);
  }
  pixels.clear();
  pixels.show();
}