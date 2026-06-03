# ESP32 Word Clock

A handmade LED word clock built on an ESP32-C3, displaying time in natural language using 144 NeoPixel LEDs. Mounts vertically or horizontally. Colors shift by season and holiday automatically.

![Word Clock](photos/clock.jpg)

---

## How It Works

The clock displays time as words — *"TWENTY FIVE PAST THREE"* — lit across a custom 144-LED grid. The LED layout is unique: every minute of the day is represented by a specific combination of illuminated letters, in two orientations (vertical 4×36, horizontal 36×4) using the same physical LED strip.

Time is synced via NTP over Wi-Fi. Colors change automatically by season, with holiday overrides for Valentine's Day, St. Patrick's Day, July 4th, Halloween, Thanksgiving, and Christmas. June is Pride rainbow. April Fools' Day displays a random wrong time in a random palette every minute.

A configurable birthday date triggers rainbow mode on that day.

---

## Hardware

| Part | Detail |
|------|--------|
| Microcontroller | ESP32-C3 Super Mini (no pins soldered — wires direct to board) |
| LED panels | 3× 4×12 NeoPixel panels (WS2812B), chained = 144 LEDs total |
| Data pin | GPIO 4 |
| Button pin | GPIO 9 (BOOT button) |
| Power | 5V via USB-C — standard adapter only, **no quick-charge** |
| 3D printed parts | See STL files in `/stl` |
| Other | Hookup wire, solder, thick clear glue (e.g. Gorilla Glue Clear) |

**Wiring:** 5V → LED 5V, GND → LED GND, GPIO 4 → LED Data In.

**Do not solder header pins to the ESP32-C3.** Solder wires directly into the through-holes. On vertical builds, solder from the back of the board. On horizontal builds, solder from the front.

---

## LED Panels & Addressing

The three 4×12 NeoPixel panels are chained together and addressed by the ESP32-C3 as a single continuous 144-LED array (LED 0–143). No special configuration is needed beyond `NUMPIXELS 144` in the code — the panels are transparent to the firmware.

Each panel has a data-in connector on one end and data-out on the other, with arrows on the PCB showing the LED data flow direction (boustrophedon — snaking back and forth across rows). Chain them so data flows continuously from panel 1 through panel 3.

**LED 0 location:**
- **Vertical:** bottom-right of the assembled clock
- **Horizontal:** top-right of the assembled clock

The mounting clips on the panels must be removed before fitting them into the word panel pockets — they take up too much space. After removing clips, solder the panels together: Data Out of panel 1 → Data In of panel 2, and so on. Solder 5V and GND in parallel across all three panels.

---

## 3D Printed Parts

Each orientation has its own set of STL files.

### Vertical
| File | Description |
|------|-------------|
| `v_panel_1.stl` | Word panel 1 of 3 |
| `v_panel_2.stl` | Word panel 2 of 3 |
| `v_panel_3.stl` | Word panel 3 of 3 |
| `v_base_1.stl` | Back panel 1 of 2 |
| `v_base_2.stl` | Back panel 2 of 2 |

### Horizontal
| File | Description |
|------|-------------|
| `h_panel_1.stl` | Word panel 1 of 3 |
| `h_panel_2.stl` | Word panel 2 of 3 |
| `h_panel_3.stl` | Word panel 3 of 3 |
| `h_base_1.stl` | Base panel 1 of 2 |
| `h_base_2.stl` | Base panel 2 of 2 |
| `h_base_3.stl` | Stand |

The base parts are designed as single pieces but may need to be split if they exceed your printer's build volume. Glue split pieces together before assembly. The horizontal stand (`h_base_3`) attaches to the base with a flat glue joint.

---

## Printing the Word Panels

The word panels are the trickiest part. Each panel has letters on the front face and a grid of LED pockets on the back that the LED panels seat into.

**Print face down** (letters on the bed). No supports needed.

### Color Change Sequence
The panels require two filament color changes:

1. **Black** — letters print first, face down on the bed
2. **White** — flat backing layer; this is the light diffuser that lets LED glow through the letters while blocking bleed between them
3. **Black** — final top layer(s) seal the back and prevent light bleed around the outside

The exact layer numbers depend on your slicer settings and layer height. Preview in your slicer before printing — you're looking for the transition where the letters end, the flat diffuser layer begins, and then the solid back begins.  See **clock_photos/Print_Detail.png**

The base parts (`v_base`, `h_base`, `h_stand`) print in black only — no color change needed.

### Glue
Use a thick, non-runny clear adhesive (Gorilla Glue Clear works well). Runny glues will wick into the LED pockets and onto the LEDs. Glue is used for all joints: LED panels into word panel pockets, word panels to each other, word panel assembly to base, and base panels to each other.

---

## ESP32-C3 Mounting

The ESP32-C3 sits in a dedicated pocket in the base. **Orient it carefully before gluing** — the BOOT button must align with the access hole and the USB-C port must remain accessible. Once glued there is no repositioning it.

- **Horizontal:** ESP32-C3 pocket is inside the base housing, board tucked in with wires soldered from the front
- **Vertical:** ESP32-C3 pocket is on the outside back of the base, more exposed, with the button hole above it and wires soldered from the back of the board

---

## Test Before You Glue

**Do this before any permanent assembly.**

Wire up the chained LED panels to the ESP32-C3. Flash the included test sketch for your orientation — `Horizontal_Word_Test.ino` or `Vertical_Word_Test.ino`. Each sketch cycles through every defined word, lighting the correct LEDs for one second each and printing the word name and LED indices to Serial.

Verify every word lights up in the right position on the physical panels before proceeding. Once the LED panels are glued into the word panel pockets there is no going back without damage.

---

## Assembly Order

1. **Print word panels** — black/white/black color change, face down, no supports
2. **Print base panels** — black only; split and glue if needed for your print bed
3. **Prep LED panels** — remove mounting clips, solder panels into a chain, solder leads to ESP32-C3
4. **Test** — flash the appropriate word test sketch, verify all words light up correctly
5. **Seat LEDs** — press LED panels into the pockets on the back of the word panels, glue in place
6. **Join word panels** — glue the three word panels together into a single strip
7. **Mount ESP32-C3** — orient carefully so BOOT button and USB-C align with access points, glue in place
8. **Attach base** — glue base panels together, then glue to the back of the word panel assembly
9. **Horizontal only** — glue stand (`h_base_3`) to base with flat joint

---

## Software Setup

### Arduino IDE Board Settings
- Board: **ESP32C3 Dev Module**
- USB CDC On Boot: **Enabled** (required for Serial monitor)

### Libraries Required
- `WiFiManager` (tzapu)
- `Adafruit NeoPixel`
- `Preferences` (built-in ESP32)

### First Upload
1. Clone or download this repo
2. Open `esp32-word-clock.ino` in Arduino IDE
3. Select correct board and port
4. Upload

---

## First-Time Setup

On first power-up the clock broadcasts a Wi-Fi network called **Word Clock Setup**.

1. Connect your phone to **Word Clock Setup** (no password) -- see **clock_photos/WiFi_Config.png**
2. The setup portal opens automatically — or go to `192.168.4.1`
3. Select your Wi-Fi network and enter password
4. Choose your **timezone** (US only: Eastern, Central, Mountain, Pacific, Alaska, Hawaii)
5. Choose **layout** (Vertical or Horizontal — must match your physical build)
6. Set your **birthday month/day** for rainbow mode on that date
7. Save — clock restarts and displays correct time within 30 seconds

**2.4 GHz only.** If your router shares a name for both bands, separate them in router settings.

---

## The Setup Button

There's a small hole on the back near the USB-C port. Use a straightened wire to press the button through it.

| Action | Result |
|--------|--------|
| Press at any time | Reopens setup portal |
| Hold 5 seconds at startup | Clears saved Wi-Fi credentials |

---

## Color Palettes - 

| Period / Holiday | Colors |
|-----------------|--------|
| Winter (Dec 22 – Mar) | Blues |
| Spring (Mar – Jun) | Pinks, yellows |
| Summer (Jun – Sep 21) | Teals, greens |
| Fall (Sep 22 – Dec 21) | Oranges, reds |
| Valentine's Day (Feb 14) | Pinks, reds |
| St. Patrick's Day (Mar 17) | Greens, gold |
| July 4th | Red, white, blue per letter |
| Halloween (all October) | Orange, purple |
| Thanksgiving | Warm ambers |
| Christmas (all December) | Red, green, gold |
| Pride (all June) | Rainbow per letter |
| Birthday | Rainbow per letter |
| April Fools' | Random wrong time, random palette |

---

## Build Notes & Gotchas

**New ESP32-C3 modules may need a manual reset after first upload.** Code will compile and upload successfully but the clock won't run. If this happens, press RESET on the board after upload completes. If it still doesn't run, hold BOOT, press RESET, release RESET, release BOOT — then upload again and press RESET when done. This may take several attempts on a fresh module. It's a quirk of the C3's USB CDC boot sequence, not a code or wiring problem.

**Test before you glue.** Once the LED panels are glued into the word panel pockets there's no going back without damage. Flash the word test sketch and verify every word first.

**Orient the ESP32-C3 before gluing.** The BOOT button and USB-C port must align with their access points in the base. Dry-fit before committing.

**NeoPixel data chain is fragile.** If only part of your strip lights up after assembly, suspect a broken LED interrupting the data line — not a wiring or code problem. The word test sketch will help isolate exactly where the chain breaks.

**Remove the LED panel clips before fitting.** The mounting clips on the 4×12 panels prevent them from seating into the word panel pockets. Remove them first.

**Use thick glue.** Runny adhesives wick into the LED pockets and onto the LEDs. Use a thick clear adhesive that stays where you put it.

**LED_PIN name collision.** On ESP32-C3, `LED_PIN` is defined by the core. The code uses `#define LED_PIN 4` which overrides it — don't rename it to something the core also defines.

**Layout must match your physical build.** Vertical and horizontal use different LED index maps. Set the correct layout in the portal or the words will be scrambled.

**Power supply matters.** 144 LEDs can draw significant current. Use a decent 5V/2A+ adapter. Quick-charge adapters can cause instability.

**Wi-Fi watchdog.** If the clock loses Wi-Fi for 60 seconds it reboots automatically and reconnects. Normal behavior, not a bug.

**NTP sync on first boot.** The clock tries up to 20 NTP sync attempts. If time is wrong on first boot, open the portal, re-save, and let it retry.

---

## Repository Structure

```
esp32-word-clock/
├── esp32-word-clock.ino
├── Horizontal_Word_Test.ino
├── Vertical_Word_Test.ino
├── stl/
│   ├── v_panel_1.stl
│   ├── v_panel_2.stl
│   ├── v_panel_3.stl
│   ├── v_base_1.stl
│   ├── v_base_2.stl
│   ├── h_panel_1.stl
│   ├── h_panel_2.stl
│   ├── h_panel_3.stl
│   ├── h_base_1.stl
│   ├── h_base_2.stl
│   └── h_base_3.stl
├── clock_photos/
│   └── WiFi_Config.png
│   ├── Vertical_ON.jpg
│   ├── Veritcal_OFF.jpg
│   ├── Horizontal_ON.jpg
│   ├── Horizontal_OFF.jpg
│   ├── Vertical_Hanger-Hook.jpg
│   └── Print_Detail.jpg
├── README.md
└── LICENSE
```

---

## License

Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0)

© 2026 Bob Garick

**Share and adapt freely for personal, non-commercial use. You must credit the original author and license any derivatives under the same terms. Commercial use is prohibited.**

Full license text: [LICENSE](LICENSE) | https://creativecommons.org/licenses/by-nc-sa/4.0/
