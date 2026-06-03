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

#define LED_PIN    4
#define NUMPIXELS  144
#define DELAYVAL   30000
#define BTN_PIN    9

Adafruit_NeoPixel pixels(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

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

// Birthday globals
uint8_t bdayMonth = 1;
uint8_t bdayDay   = 25;

// Layout: 0 = Vertical, 1 = Horizontal
uint8_t layout = 0;

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
int V_quarter[]    = { 101,98,93,90,85,82,77 };
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
int H_quarter[]    = { 102,97,94,89,86,81,78 };
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
bool isBirthday()   { return (month_ == (int)bdayMonth && day_ == (int)bdayDay); }
bool isPride()      { return (month_ == 6); }
bool isValentine()  { return (month_ == 2  && day_ == 14); }
bool isStPatrick()  { return (month_ == 3  && day_ == 17); }
bool isHalloween()  { return (month_ == 10); }
bool isChristmas()  { return (month_ == 12); }
bool isAprilFools() { return (month_ == 4  && day_ == 1); }

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
  Serial.println("       Word Clock -- ESP32-C3 Boot");
  Serial.println("========================================");
  Serial.printf("  LED_PIN  : GPIO%d\n", LED_PIN);
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
  bdayMonth = prefs.getUChar("bdayMonth", 1);
  bdayDay   = prefs.getUChar("bdayDay",  25);
  layout    = prefs.getUChar("layout",    0);
  prefs.end();

  if (bdayMonth < 1 || bdayMonth > 12) bdayMonth = 1;
  if (bdayDay   < 1 || bdayDay   > 31) bdayDay   = 1;
  if (layout > 1) layout = 0;

  Serial.printf("[PREFS] TZ: %s  Birthday: %02u/%02u  Layout: %s\n",
    storedTZ.c_str(), bdayMonth, bdayDay, layout == 0 ? "Vertical" : "Horizontal");

  applyLayout();

  // --- NeoPixel init ---
  Serial.println("[LED] Initializing NeoPixels...");
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

  // Birthday
  WiFiManagerParameter bdayMonthParam("bdayMonth", "", String(bdayMonth).c_str(), 4, "type='hidden'");
  wm.addParameter(&bdayMonthParam);

  WiFiManagerParameter bdayDayParam("bdayDay", "", String(bdayDay).c_str(), 4, "type='hidden'");
  wm.addParameter(&bdayDayParam);

  String bdayHtml;
  bdayHtml.reserve(1200);
  bdayHtml += F("<label>Birthday:</label><br/>");
  bdayHtml += F("<select id='bday_month_sel' style='width:48%;padding:6px;margin-right:4%;'>");
  const char* monthNames[] = {
    "January","February","March","April","May","June",
    "July","August","September","October","November","December"
  };
  for (int m = 1; m <= 12; m++) {
    bdayHtml += "<option value='"; bdayHtml += String(m); bdayHtml += "'>";
    bdayHtml += monthNames[m-1]; bdayHtml += "</option>";
  }
  bdayHtml += F("</select>");
  bdayHtml += F("<select id='bday_day_sel' style='width:48%;padding:6px;'>");
  for (int d = 1; d <= 31; d++) {
    bdayHtml += "<option value='"; bdayHtml += String(d); bdayHtml += "'>";
    bdayHtml += String(d); bdayHtml += "</option>";
  }
  bdayHtml += F("</select>");
  bdayHtml += F(R"rawliteral(
    <script>
      document.addEventListener('DOMContentLoaded', function(){
        var mSel = document.getElementById('bday_month_sel');
        var dSel = document.getElementById('bday_day_sel');
        var mHid = document.getElementsByName('bdayMonth')[0];
        var dHid = document.getElementsByName('bdayDay')[0];
        if(mSel && mHid){
          for(var i=0;i<mSel.options.length;i++){
            if(mSel.options[i].value===mHid.value){ mSel.selectedIndex=i; break; }
          }
          mSel.addEventListener('change', function(){ mHid.value=this.value; });
        }
        if(dSel && dHid){
          for(var i=0;i<dSel.options.length;i++){
            if(dSel.options[i].value===dHid.value){ dSel.selectedIndex=i; break; }
          }
          dSel.addEventListener('change', function(){ dHid.value=this.value; });
        }
      });
    </script>
    <br/><br/>
  )rawliteral");
  WiFiManagerParameter bdayHTMLParam(bdayHtml.c_str());
  wm.addParameter(&bdayHTMLParam);

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

  // Save callback
  wm.setSaveParamsCallback([&](){
    const char* tz = tzParam.getValue();

    int mTmp = atoi(bdayMonthParam.getValue());
    int dTmp = atoi(bdayDayParam.getValue());
    if (mTmp < 1 || mTmp > 12) mTmp = 1;
    if (dTmp < 1 || dTmp > 31) dTmp = 1;

    int lTmp = atoi(layoutParam.getValue());
    if (lTmp < 0 || lTmp > 1) lTmp = 0;

    prefs.begin("settings", false);
    prefs.putString("tz",        tz);
    prefs.putUChar("bdayMonth",  (uint8_t)mTmp);
    prefs.putUChar("bdayDay",    (uint8_t)dTmp);
    prefs.putUChar("layout",     (uint8_t)lTmp);
    prefs.end();

    bdayMonth = (uint8_t)mTmp;
    bdayDay   = (uint8_t)dTmp;
    layout    = (uint8_t)lTmp;
    applyLayout();

    Serial.println("[PORTAL] Settings saved:");
    Serial.printf("[PORTAL]   TZ      : %s\n", tz);
    Serial.printf("[PORTAL]   Birthday: %02d/%02d\n", mTmp, dTmp);
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

  WIFI_OK = true;

  // Reload prefs post-connect
  Serial.println("[PREFS] Reloading settings post-connect...");
  prefs.begin("settings", true);
  String timezone = prefs.getString("tz", "EST5EDT,M3.2.0/2,M11.1.0/2");
  bdayMonth = prefs.getUChar("bdayMonth", bdayMonth);
  bdayDay   = prefs.getUChar("bdayDay",   bdayDay);
  layout    = prefs.getUChar("layout",    layout);
  prefs.end();

  if (bdayMonth < 1 || bdayMonth > 12) bdayMonth = 1;
  if (bdayDay   < 1 || bdayDay   > 31) bdayDay   = 1;
  if (layout > 1) layout = 0;

  applyLayout();

  Serial.printf("[PREFS] TZ: %s  Birthday: %02u/%02u  Layout: %s  Brightness: %u (fixed)\n",
    timezone.c_str(), bdayMonth, bdayDay,
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
  pixels.clear();
  showTime(hour_, minute_);
  applyPowerLimitAndShow();

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
        bdayMonth = prefs.getUChar("bdayMonth", bdayMonth);
        bdayDay   = prefs.getUChar("bdayDay",   bdayDay);
        layout    = prefs.getUChar("layout",    layout);
        prefs.end();
        if (bdayMonth < 1 || bdayMonth > 12) bdayMonth = 1;
        if (bdayDay   < 1 || bdayDay   > 31) bdayDay   = 1;
        if (layout > 1) layout = 0;
        applyLayout();
        Serial.printf("[BTN] Applied TZ: %s  Birthday: %02u/%02u  Layout: %s\n",
          timezone.c_str(), bdayMonth, bdayDay,
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

    pixels.clear();
    showTime(dispHour, dispMin);
    applyPowerLimitAndShow();

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
  const bool july4   = g_forceJuly4   || isJuly4();
  const bool rainbow = g_forceRainbow || isPride() || isBirthday();
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
    pixels.setPixelColor(idx, pixels.Color(rr,gg,bb));
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
