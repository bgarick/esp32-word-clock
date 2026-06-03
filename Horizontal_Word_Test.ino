/*
  WordClockHorizTest.ino
  Cycles through every defined HORIZONTAL word, one at a time, 1 second each.
  Prints the word name and indices to Serial.

  Board: ESP32C3 Dev Module, USB CDC On Boot: Enabled
  LED_PIN: 4   NUMPIXELS: 144
*/

#include <Adafruit_NeoPixel.h>

#define LED_PIN    4
#define NUMPIXELS  144

Adafruit_NeoPixel pixels(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

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

struct Word { const char* name; int* arr; int len; };

Word words[] = {
  { "ONE(min)",    H_oneMin,     3 },
  { "TWO(min)",    H_twoMin,     3 },
  { "THREE(min)",  H_threeMin,   5 },
  { "FOUR(min)",   H_fourMin,    4 },
  { "FIVE(min)",   H_fiveMin,    4 },
  { "SIX(min)",    H_sixMin,     3 },
  { "SEVEN(min)",  H_sevenMin,   5 },
  { "EIGHT(min)",  H_eightMin,   5 },
  { "NINE(min)",   H_nineMin,    4 },
  { "TEN(min)",    H_tenMin,     3 },
  { "ELEVEN(min)", H_elevenMin,  6 },
  { "TWELVE(min)", H_twelveMin,  6 },
  { "THIRTEEN",    H_thirteen,   8 },
  { "FOURTEEN",    H_fourteen,   8 },
  { "FIFTEEN",     H_fifteen,    7 },
  { "QUARTER",     H_quarter,    7 },
  { "SIXTEEN",     H_sixteen,    7 },
  { "SEVENTEEN",   H_seventeen,  9 },
  { "EIGHTEEN",    H_eighteen,   8 },
  { "NINETEEN",    H_nineteen,   8 },
  { "TWENTY",      H_twenty,     6 },
  { "HALF",        H_half_,      4 },
  { "AFTER",       H_after,      5 },
  { "PAST",        H_past,       4 },
  { "TIL",         H_til,        3 },
  { "OF",          H_of,         2 },
  { "TO",          H_to_,        2 },
  { "BEFORE",      H_before,     6 },
  { "ONE(hr)",     H_oneHour,    3 },
  { "TWO(hr)",     H_twoHour,    3 },
  { "THREE(hr)",   H_threeHour,  5 },
  { "FOUR(hr)",    H_fourHour,   4 },
  { "FIVE(hr)",    H_fiveHour,   4 },
  { "SIX(hr)",     H_sixHour,    3 },
  { "SEVEN(hr)",   H_sevenHour,  5 },
  { "EIGHT(hr)",   H_eightHour,  5 },
  { "NINE(hr)",    H_nineHour,   4 },
  { "TEN(hr)",     H_tenHour,    3 },
  { "ELEVEN(hr)",  H_elevenHour, 6 },
  { "TWELVE(hr)",  H_twelveHour, 6 },
  { "NOON",        H_noon,       4 },
  { "MIDNIGHT",    H_midnight,   8 },
  { "OCLOCK",      H_oclock,     6 },
  { "WIFI_ANIM",   H_wifiAnim,   4 },
  { "NO_ANIM",     H_noAnim,     2 },
};

const int WORD_COUNT = sizeof(words) / sizeof(words[0]);

void showWord(Word& w) {
  pixels.clear();
  Serial.printf("[WORD] %-14s =>", w.name);
  for (int i = 0; i < w.len; i++) {
    int idx = w.arr[i];
    Serial.printf(" %d", idx);
    if (idx >= 0 && idx < NUMPIXELS) {
      pixels.setPixelColor(idx, pixels.Color(180, 180, 50));
    } else {
      Serial.print("(OOB!)");
    }
  }
  Serial.println();
  pixels.show();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n[DIAG] WordClock Horizontal Word Test");
  Serial.printf("[DIAG] %d words defined. Cycling 1s each.\n", WORD_COUNT);

  pixels.begin();
  pixels.setBrightness(30);
  pixels.clear();
  pixels.show();
}

void loop() {
  for (int i = 0; i < WORD_COUNT; i++) {
    showWord(words[i]);
    delay(1000);
  }
  pixels.clear();
  pixels.show();
  Serial.println("\n[DIAG] --- cycle complete ---\n");
  delay(500);
}
