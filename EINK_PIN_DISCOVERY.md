# pebl ESP32-S3 — E-Ink Display Pin Discovery Notes

Working notes on reverse-engineering the GPIO pin mapping for the e-ink display
on a pre-flashed **pebl.ink** device, after the factory firmware (which held the
pin mapping) was erased.

**Goal:** drive the e-ink display from custom firmware (e.g. write "WILL").

_Last updated: 2026-06-13_

---

## Hardware

| Item | Detail |
|------|--------|
| MCU | ESP32-S3-WROOM-1 (4 MB flash, quad SPI, 3.3 V) |
| Valid GPIOs | 0–21, 26–48 (GPIO 22–25 don't exist; 26–32 = flash; 19/20 = USB; 39 = boot button) |
| Display panel | **DEPG0213BN**, label "0213BN-B74", 212 × 104 px, 2-level B/W |
| Display controller | **SSD1680** |
| Flex cable | FPC-A002 (dated 20.04.08), 24-pin, contacts on one side, black stiffener back |
| PlatformIO env | `lilygo_t5_s3_depg_bw` (board `esp32-s3-devkitc-1`) |
| Serial port | `/dev/cu.usbmodem101` (native USB CDC) |

Flash command:
```
cd /Users/willbraden/Documents/Development/pebl-esp32
~/.platformio/penv/bin/pio run -e lilygo_t5_s3_depg_bw --target upload
```

---

## ✅✅ SOLVED (2026-06-13) — "WILL" RENDERED CLEANLY

**FINAL CONFIRMED PINOUT** (verified by rendering text via GxEPD2_213_B74):

| Signal | GPIO |
|--------|------|
| PWR (power enable) | **13** |
| RST | **8** |
| CS | **16** |
| DC | **12** |
| BUSY | **14** |
| SCLK | **10** |
| MOSI | **9** |

Working code: `GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT> disp(GxEPD2_213_B74(16, 12, 8, 14));`
with `pinMode(13, OUTPUT); digitalWrite(13, HIGH);` (power) and `SPI.begin(10, -1, 9, -1);`.
Then `disp.init(); disp.setRotation(1);` and draw with Adafruit GFX as normal.

(History of how we got here is below.)

## ⭐ BREAKTHROUGH (2026-06-13, session 2) — NEARLY SOLVED

Found the pins via a **BUSY-pulse feedback** method (watch the panel's BUSY line for a
real ~2-second refresh pulse) plus a **data-path test** (write all-white, watch the
screen actually go uniform).

**CONFIRMED pinout (all but DC nailed down):**
| Signal | GPIO | How confirmed |
|--------|------|---------------|
| PWR (power enable) | **13** | drives panel |
| RST | **8** | BUSY-pulse hit (`dur=2102ms` real refresh) |
| CS | **16** | BUSY-pulse hit |
| BUSY | **14** | BUSY-pulse hit (idles low, base=0) |
| SCLK | **10** | BUSY-pulse hit + recurred in every flash |
| MOSI | **9** | BUSY-pulse hit |
| **DC** | **~12 (UNCONFIRMED)** | see below |

**The DC discovery:** with DC held low, commands worked (refreshes fired, BUSY responded)
but pixel data never landed → frozen speckle. Sweeping a candidate DC pin and writing
all-white, **the display finally went UNIFORM WHITE** (speckle gone, QR gone) — proving
the data path. DC is one of the pool pins; **best guess GPIO12** (where the screen first
went uniform in the sweep). NEXT STEP: confirm DC by writing a clean half/half with
DC=12 (then 11, 7, etc. if 12 is wrong). Once DC is confirmed, render text.

**Earlier wrong turn:** RST=14 was WRONG (14 is actually BUSY). That's why session-1
sweeps assuming RST=14 all failed.

**Init note:** hand-rolled SSD1680 init must include `0x18=0x80` (temp sensor) and
`0x21=0x00,0x80` (display update control) — was missing these. Slow bit-bang clock
(`delayMicroseconds(3)` per phase) works fine. Once DC confirmed, can switch to the real
`GxEPD2_BW<GxEPD2_213_B74>` driver with these pins for easy text rendering.

---

## CONFIRMED facts (high confidence)

- ✅ **Panel is alive** and the **FPC connector works** — proven by continuity test:
  GPIO14 reads driven-HIGH with the ribbon seated, floats when removed, returns
  HIGH on reseat (repeatable).
- ✅ **PWR (panel power enable) = GPIO13.** Driving it HIGH powers the panel;
  toggling it changes GPIO14's state. Must be HIGH for any display activity.
- ✅ The panel **does respond** to SPI activity — specific pin combos trigger a
  visible e-ink **refresh ("flash")**, so commands partly reach it.
- ✅ The frozen "WiFi Setup" QR code that was on screen at the start was a
  **retained factory image** (e-ink holds images with no power) — not proof of a
  live connection.

## STRONG candidates / partial evidence

| Signal | Candidate | Evidence | Confidence |
|--------|-----------|----------|------------|
| PWR | **GPIO13** | drives panel, moves GPIO14 | High |
| RST | **GPIO14** | panel-side pull-up, continuity-proven panel pin | Medium* |
| SCLK | **GPIO10** | recurs in every combo that triggers a refresh | Medium |
| MOSI | **GPIO9** | recurs in every combo that triggers a refresh | Medium |
| CS / DC | among **{8, 16, 18}** | board/panel pull-ups; flashes seen with these | Low–Med |

\* RST=14 is in doubt: 2 full sweep passes assuming RST=14 found no clean image.

## Pin electrical fingerprint (with panel powered, GPIO13 HIGH)

| GPIO | Behavior | Likely role |
|------|----------|-------------|
| 13 | drives panel power | PWR |
| 14 | panel-side pull-up (floats when ribbon removed) | RST or CS |
| 16 | **board-side** pull-up (stays HIGH even ribbon-out) | CS / DC input? |
| 18 | **board-side** pull-up (stays HIGH even ribbon-out) | CS / DC input? |
| 8 | floats when unpowered; reads HIGH when panel powered | BUSY? / input? |
| pool pins (1–7,9–12,15,17,21) | idle low, MCU-driven, no pull-up | SCLK / MOSI candidates |

## RULED OUT

- ❌ Repo default pins (CS=5, DC=17, RST=16, BUSY=4, SCLK=18, MOSI=23) — those are
  LilyGo T5 pins; MOSI=23 doesn't even exist on the S3.
- ❌ Static write with `CS=16, DC=8, SCLK=10, MOSI=9` (both fast and slow clock):
  no change to panel → that exact combo is wrong.
- ❌ Full constrained sweep `RST=14 / CS,DC∈{8,16,18} / SCLK,MOSI∈pool` with slow
  clock, run **2 full passes** (~1.5 h): zero clean images. The winning combo is
  **not** in this space, so at least one assumption above is wrong.
- ❌ BUSY-feedback approach: **no readable BUSY output found.** v23 (reset pulse,
  watch for high→low transient) and v24 (pulse each special pin as RST, watch
  others) both found nothing — only static-high pull-ups on 8/16/18. The panel
  gives no electrical feedback to validate guesses against.

---

## Why this is hard

E-ink control pins (CS, DC, SCLK, MOSI) are **high-impedance inputs** — they accept
signals but send nothing back, so they're invisible to electrical probing. The only
panel pin findable passively was RST (it has a pull-up). BUSY (the one output) was
never detectable. With no feedback signal, a correct-but-imperfect pin guess can't be
distinguished from a wrong one — every wrong combo produces visually identical
**speckle/noise**, and brute-forcing 4+ interdependent pins blind is a search space
in the thousands that has not converged.

## The reliable fix (next step)

**Multimeter continuity trace** (~15 min, definitive):
1. Set multimeter to continuity / beep mode.
2. Identify the 24 gold pins on the FPC connector (where the ribbon plugs in).
3. Touch one probe to a connector pin, the other to each ESP32 GPIO pad, until it
   beeps — that maps the pin.
4. Map the ~6 signal pins (RST, CS, DC, SCLK, MOSI, BUSY); plug those into the
   firmware's `einkPinScan()` / a proper `GxEPD2` driver instance.

The SSD1680 + GxEPD2 driver (`GxEPD2_213_B74`, 122×250 buffer) will work once the
pins are correct.

---

## Method / tooling notes (for resuming)

- Test firmware lives in `src/main.cpp`, in a replaced `einkPinScan()` called from
  `setup()`. Bit-bang SPI helpers: `e7Byte / e7Cmd / e7Data` (slow clock =
  `delayMicroseconds(3)` per phase — keep this).
- A webcam watcher (`/tmp/camwatch`, Swift app `BandCam2.app`) auto-detects clean
  vs noise patterns via ROI row/col-amplitude + downsampled variance. **Recompiling
  it breaks macOS camera permission** — needs a fresh bundle id + re-grant each time.
- Serial loggers are Python (`pyserial`); flag combos by timestamp and correlate to
  camera events.
- Persistent memory note: `pebl-eink-pinout-progress.md` in the Claude project memory dir.
