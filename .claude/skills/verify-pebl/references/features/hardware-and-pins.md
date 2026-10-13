# Hardware and pins

The board, every GPIO we know the job of, and how each fact was established. There is no schematic. Everything here was found by probing, so each row says how it was confirmed.

## Sub-features

### Core parts

| Part | Detail | Source |
|---|---|---|
| MCU | ESP32-S3-WROOM-1 module, 4 MB flash (boots `mode:DIO`) | esptool, boot ROM log |
| USB | Native USB-Serial/JTAG on GPIO19 (D−) / GPIO20 (D+). No USB-UART bridge chip | boot log `USB_UART_CHIP_RESET`, port `/dev/cu.usbmodem*` |
| Display | 2.13" black/white e-ink, SSD1680 controller, glass label "0213BN-B74", 24-pin FPC "FPC-A002" | label on the flex |
| Display driver | `GxEPD2_BW<GxEPD2_213_B74>`, drawn at **250 × 122** with `setRotation(3)` | border test fills the panel exactly |
| Display power boost | Inductor marked "470", SOT-23 MOSFET, three SOD diodes marked "SE", "2R2" sense resistor, all beside the FPC. This is the panel's high-voltage supply, not the 3.3 V rail | photo, matches the standard e-paper reference circuit |

`EINK_PIN_DISCOVERY.md` lists the panel as 212 × 104 and the boot button as GPIO39. Both are wrong for this board. The firmware renders 250 × 122, and BOOT is GPIO0 (button probe, 2026-10-12).

### Display pins (confirmed by rendering)

| Signal | GPIO | Notes |
|---|---|---|
| PWR (panel power enable) | 13 | Drive HIGH and wait (~300 ms in `initDisplay()`) before any SPI |
| RST | 8 | |
| CS | 16 | **Shared with USER SW.** Don't pass it to `SPI.begin()`; GxEPD2 drives it in software |
| DC | 12 | Found last. With DC wrong, commands work but pixel data never lands |
| BUSY | 14 | Input. Reads pulled-high at rest |
| SCLK | 10 | |
| MOSI | 9 | No MISO |

### Buttons and switches

| Control | Wiring | Notes |
|---|---|---|
| RESET | Chip enable | Full reset. Firmware sees `wake=0 (power-on/reset)` |
| BOOT (silkscreen reads like "3001"/"BOOL") | GPIO0, active low | Strapping pin: held during reset → download mode (`boot:0x0`). After boot it's a normal input; reads floating with internal pulls |
| USER SW | **GPIO16, active low**, board pull-up | Same line as display CS. RTC-capable, so it can wake deep sleep (ext0). Found by `pebl_btnprobe` pass 3 |
| Power switch | Cuts battery power | With USB plugged in the board stays powered |

### Every other GPIO

| GPIO | State at rest | Status |
|---|---|---|
| 1–7 | Floating (ADC reads 50–300 mV) | Free. ADC1 ch0–6. GPIO1 is the suggested battery-sense pin |
| 11, 15, 17 | Floating | Free (ADC2, unusable while WiFi is on) |
| 18 | **External pull-up**, ADC full scale | Unknown job. Not USER SW, not VBUS detect, didn't move during an 8 h drain |
| 21 | Floating | Free |
| 26–32 | | SPI flash. Never touch |
| 33, 34 | | Not broken out on WROOM-1 |
| 35–37 | Floating | Free on this module as far as probed (they'd be PSRAM on R8 variants) |
| 38–42, 45–48 | Floating | Free (not ADC). 45/46 are strapping pins: don't pull them at reset |
| 43, 44 | Floating | UART0 TX/RX, unused (console is on USB) |

Deep-sleep wake (ext0/ext1) works only on RTC GPIOs 0–21.

### Unidentified parts

- QFN-16 marked "AKK / BKF" next to the green 2-pin screw terminal (−/+). Function unknown; no I2C device answered on any pulled-up pin pair.
- Two SOT-23-5 parts marked "2U" near USB-C, probably regulators. No public marking match.
- Green LED near USB-C, seen lit on USB power. Not known to be software-controlled.
- Red LED: probably charge status (on while charging, off when full). It went dark while USB data kept flowing, and came back after a replug, which is typical of a charger restarting a cycle. Not readable by firmware.
- The green screw terminal's purpose (battery input or external power) is unverified.

## How to get to it (user POV)

Board face up, panel on the right: USB-C at the top edge, the three buttons and the power switch along the edge, the FPC on the right, the screw terminal top-left. Ask the user for a straight-down macro photo with good light when part markings matter. HEIC photos convert with `sips -s format jpeg in.HEIC --out out.jpg`, and need `ImageOps.exif_transpose` before cropping.

## Driving it

- Pin questions: rerun `pebl_btnprobe` (classifies every pin as float/GND/3V3 under both pulls and prints changes) or `pebl_battprobe` (ADC + I2C scan). See `diagnostic-firmwares.md`.
- To prove a new pin assignment, show the probe log line, such as `G16: 3V3 -> GND` on each press.

## Gotchas

- Any probe that runs `Wire.end()` leaves pins with internal pull-ups on (`gpio_reset_pin`). Floating ADC pins then read full scale. Read ADC first, or disable pulls explicitly.
- Classic-ESP32 ADC channel constants (`ADC1_CHANNEL_7` = "GPIO35") map to different pins on the S3. `src/main.cpp`'s battery code makes this mistake.
- Pressing USER SW while the panel refreshes shorts the driven-high CS to ground. The firmware sets GPIO16 to `GPIO_DRIVE_CAP_0` to limit that current; keep it if you touch `initDisplay()`.
