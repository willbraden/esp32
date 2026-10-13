---
name: verify-pebl
description: "Build, flash, drive and prove changes on the pebl ESP32-S3 e-ink weather display (env pebl_weather). Use when verifying firmware behavior on the real board: flashing and recovery, serial evidence, wake/sleep, the numbers and word screens, the USER SW mode button, and layout changes via tools/weather_design.py. Also the reference for the board's pin map and hardware."
---

# Verify pebl

Prove firmware changes on the real pebl board: an ESP32-S3-WROOM-1 driving a 2.13" black-and-white e-ink panel, running the standalone weather firmware in `src/weather/`. There is no screen capture. Evidence is the serial log, the design tool's pixel-exact previews, and a photo of the panel.

The hardware facts (pin map, buttons, what is and is not on the board) are in [`references/features/hardware-and-pins.md`](references/features/hardware-and-pins.md). Read that before touching any GPIO.

## Pick a flow

Run `ls /dev/cu.usbmodem*` first. Whether a port exists tells you what state the chip is in.

- **Port present, firmware awake** (a probe firmware, or the weather firmware mid-cycle): `pio ... -t upload` resets it into the bootloader over USB on its own.
- **No port:** the weather firmware is in deep sleep (normal), or the cable is charge-only. Ask the user to **hold BOOT, tap RESET, release BOOT**, then flash. After the flash the chip sits in download mode. Start it with `esptool ... --after watchdog_reset chip_id` or ask for one RESET tap.
- **Layout-only change:** run `tools/weather_design.py` and read the previews before flashing anything. The previews match the firmware pixel for pixel.

## Quick start

```bash
cd /Users/willbraden/Documents/Development/pebl-esp32
PIO=~/.platformio/penv/bin/pio
PY=~/.platformio/penv/bin/python          # has pyserial, freetype-py, pillow
PORT=$(ls /dev/cu.usbmodem* | head -1)    # 101 or 1101 depending on the USB socket

$PIO run -e pebl_weather                                   # build
$PIO run -e pebl_weather -t upload --upload-port $PORT     # flash
$PY ~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32s3 --port $PORT \
    --after watchdog_reset chip_id                         # leave download mode without touching the board
$PY tools/serial_log.py --quiet --until Sleeping --timeout 90   # one full wake cycle
$PY tools/weather_design.py                                # regenerate fonts/word art + previews
```

## Command surface

- **Build/flash:** `pio run -e <env> [-t upload --upload-port <port>]`. Envs: `pebl_weather` (the product), `pebl_weather_staging` (1-minute wakes, local OTA server), `pebl_battprobe`, `pebl_btnprobe`, `pebl_netstress`, `pebl_otafail` (diagnostics).
- **Release (OTA):** `git tag weather-vX.Y.Z && git push willbraden main weather-vX.Y.Z`. CI builds, signs and publishes; devices pick it up within ~2 h. See `ota-updates.md`.
- **Unit tests:** `clang++ -std=c++17 -I src/weather tools/test_semver.cpp -o /tmp/t && /tmp/t`. The `lilygo_*` envs are the legacy pebl client and are not verified on this board.
- **Leave download mode:** `esptool.py --chip esp32s3 --port <port> --after watchdog_reset chip_id`. A plain `--after hard_reset` leaves this chip in download mode.
- **Serial:** `tools/serial_log.py [--until TEXT] [--timeout S] [--quiet] [--port P]`. It reconnects across deep sleep and opens with DTR/RTS deasserted.
- **Design:** `tools/weather_design.py` writes `src/weather/weather_fonts.h`, `src/weather/weather_words.h`, `tools/preview.png` and `tools/preview_words.png`, and prints the worst-case gaps.
- **Crash decode:** `~/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-addr2line -pfiaC -e .pio/build/<env>/firmware.elf <addrs>`. Check the `ELF file SHA256:` prefix in the panic against `shasum -a 256 firmware.elf` first. A different build decodes to the wrong lines.

## Proof bar

A claim about the device needs device evidence. "It compiled" and "the preview looks right" are not proof of on-device behavior.

- Show the serial lines that prove the path ran. The firmware prints a stable marker for each step, listed in [`serial-diagnostics.md`](references/features/serial-diagnostics.md): wake cause, mode, cache vs fetch, the reading, `_Update_Full`, `Sleeping`.
- For anything visual, show the design-tool preview and ask the user for a photo of the panel. The preview proves the pixels; the photo proves the panel actually refreshed.
- Exercise every path the change touches: cold boot, timer wake, USER SW wake (cached and stale cache), WiFi failure, fetch failure, and both screens. The feature file lists the paths.
- A run that crashed and recovered is not a pass. Search the log for `assert failed`, `Guru Meditation`, `Rebooting...` and `rst:0xc` before calling a run clean.
- Persistence claims (mode survives power-off) need a real power cycle with the switch, not just a RESET.

## Driving conventions

- Never hold the serial port while flashing. Kill stale loggers first (`lsof /dev/cu.usbmodem*`).
- Reopening the port after the chip re-enumerates can reset it. A logger attached during a USER SW test turns the button wake into a "power-on/reset" boot. Prove button wakes on screen, or attach the logger only after the press.
- Don't poll the port to "wake" the board. In deep sleep there is nothing to talk to.
- When adding layout, change `tools/weather_design.py` first, read the previews, then copy the knobs into `weather_main.cpp`. The two must stay in sync. Each side says so in comments.
- GPIO16 is both the display's CS and the USER SW button. Read the button only while the display is idle or asleep. Never configure it as a plain output outside `initDisplay()`.

## Feature map

The behavior inventory lives in [`references/features/`](references/features/). Each file uses the same four H2s: `Sub-features`, `How to get to it (user POV)`, `Driving it`, `Gotchas`. For a broad regression, walk the [README](references/features/README.md) top to bottom, then [`multi-surface-journeys.md`](references/features/multi-surface-journeys.md).
