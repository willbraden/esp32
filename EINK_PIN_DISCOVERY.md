# pebl ESP32-S3 — E-Ink Display Pin Discovery

How the e-ink display's GPIO mapping was reverse-engineered on a pebl.ink board after
the factory firmware (the only record of the mapping) was erased. Solved 2026-06-13.

The current, complete board reference (buttons, LEDs, what isn't on the board) is
`.claude/skills/verify-pebl/references/features/hardware-and-pins.md`.

## Result

| Signal | GPIO |
|--------|------|
| PWR (panel power enable) | **13** |
| RST | **8** |
| CS | **16** (also the USER SW button, found later) |
| DC | **12** |
| BUSY | **14** |
| SCLK | **10** |
| MOSI | **9** |

```cpp
pinMode(13, OUTPUT); digitalWrite(13, HIGH);          // panel power first
SPI.begin(10, -1, 9, -1);                             // SCLK, no MISO, MOSI, no hw CS
GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT> disp(GxEPD2_213_B74(16, 12, 8, 14));
disp.init(); disp.setRotation(3);                     // 250 x 122 landscape
```

## Hardware

| Item | Detail |
|------|--------|
| MCU | ESP32-S3-WROOM-1 (4 MB flash, quad SPI, 3.3 V) |
| Valid GPIOs | 0–21, 26–48 (22–25 don't exist; 26–32 = flash; 19/20 = USB) |
| Display | 2.13" B/W e-ink, glass label "0213BN-B74", SSD1680 controller, driven as 250 × 122 by `GxEPD2_213_B74` |
| Flex cable | FPC-A002 (dated 20.04.08), 24-pin, contacts on one side |

An earlier draft listed the panel as 212 × 104 (the LilyGo DEPG0213BN default) and the
BOOT button as GPIO39. Both were wrong for this board: it renders 250 × 122, and BOOT is
GPIO0.

## Method that worked

1. **Find PWR first.** With GPIO13 driven HIGH the panel powers up, and GPIO14's level
   changes. Nothing responds while it's LOW.
2. **BUSY-pulse feedback.** For each candidate RST/CS/SCLK/MOSI combination, send a
   reset + full-refresh sequence and watch candidate BUSY pins for a real ~2 s high
   pulse (`dur=2102ms`). A real pulse means the commands landed. This found RST 8,
   CS 16, BUSY 14, SCLK 10, MOSI 9.
3. **Data-path test for DC.** With DC wrong, commands still work (the panel refreshes,
   BUSY pulses) but pixel data never lands, so the screen stays frozen speckle.
   Sweeping DC while writing all-white turned the screen uniformly white at DC = 12.
4. **Confirm with the real driver** by rendering text through `GxEPD2_213_B74`.

The bit-banged sweep code (`einkPinScan()`, `e7Byte/e7Cmd/e7Data`, slow clock of
`delayMicroseconds(3)` per phase) is still in `src/main.cpp`, which no env builds.

## Lessons

- **E-ink control pins are invisible to passive probing.** CS, DC, SCLK and MOSI are
  high-impedance inputs. Every wrong combination produces the same speckle, so a
  near-miss looks exactly like a total miss. BUSY is the one feedback signal; use it.
- **The frozen image at the start meant nothing.** The "WiFi Setup" QR code on screen
  was a retained factory image. E-ink holds pictures without power.
- **Wrong assumptions cost the most time.** Two full sweeps (~1.5 h) assumed RST = 14
  because GPIO14 had a panel-side pull-up. GPIO14 is BUSY. Drop an assumption as soon as
  a sweep built on it comes back empty.
- **Board-side pull-ups are clues.** GPIO16 and GPIO18 stay HIGH with the ribbon
  removed. 16 turned out to be CS plus the USER SW button. 18's job is still unknown.
- **Hand-rolled SSD1680 init** needs `0x18 = 0x80` (temperature sensor) and
  `0x21 = 0x00, 0x80` (display update control), or refreshes misbehave.
- The repo's original LilyGo T5 pins (CS 5, DC 17, RST 16, BUSY 4, SCLK 18, MOSI 23)
  don't apply. MOSI 23 doesn't exist on the S3.
