---
name: verify-pebl
description: "Build, flash, drive and prove changes on the pebl ESP32-S3 e-ink weather display (env pebl_weather). Use when verifying firmware behavior on the real board: flashing and recovery, serial evidence, wake/sleep, the numbers and word screens, the USER SW mode button, and layout changes via tools/weather_design.py. Also use when shipping a firmware update to devices (weather-v* release tags, OTA). Also the reference for the board's pin map and hardware."
---

# Verify pebl

Prove firmware changes on the real pebl board: an ESP32-S3-WROOM-1 driving a 2.13" black-and-white e-ink panel, running the standalone weather firmware in `src/weather/`. There is no screen capture. Evidence is the serial log, the design tool's pixel-exact previews, and a photo of the panel.

The hardware facts (pin map, buttons, what is and is not on the board) are in [`references/features/hardware-and-pins.md`](references/features/hardware-and-pins.md). Read that before touching any GPIO.

## Pick a flow

Run `ls /dev/cu.usbmodem*` first. Whether a port exists tells you what state the chip is in.

- **Port present, firmware awake** (a probe firmware, or the weather firmware mid-cycle): `pio ... -t upload` resets it into the bootloader over USB on its own.
- **Port appears for ~10 s, then goes:** the weather firmware woke for a refresh. Auto-reset (`pio ... -t upload`, or esptool's default `--before default_reset`) still catches it. `--before no_reset` fails with `No serial data received`, because the chip isn't in download mode. Scripts that wait for the port must handle both.
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

- **Build/flash:** `pio run -e <env> [-t upload --upload-port <port>]`. Envs: `pebl_weather` (the product), `pebl_weather_staging` (1-minute wakes, local OTA server), `pebl_weather_fault` (staging plus forced Wi-Fi failures on OTA trial boots, for proving retry and rollback; never ship it), `pebl_battprobe`, `pebl_btnprobe`, `pebl_netstress`, `pebl_otafail` (diagnostics). Each env compiles only its own `src/` folder (`build_src_filter`); give any new env one too.
- **Release (OTA):** an annotated `weather-vX.Y.Z` tag pushed to the `willbraden` remote, or a hand run of the `weather-release` workflow on `main` with the version (it makes the tag). Full procedure in [Ship an update](#ship-an-update).
- **Unit tests:** `clang++ -std=c++17 -I src/weather tools/test_semver.cpp -o /tmp/t && /tmp/t`.
- **Leave download mode:** `esptool.py --chip esp32s3 --port <port> --after watchdog_reset chip_id`. A plain `--after hard_reset` leaves this chip in download mode.
- **Serial:** `tools/serial_log.py [--until TEXT] [--timeout S] [--quiet] [--port P]`. It reconnects across deep sleep and opens with DTR/RTS deasserted. `--timeout` counts from launch, not from the boot: give it room when waiting for the user.
- **Boot log:** the firmware keeps a record of its last 16 boots in NVS (how far each got and how it ended) and prints them at boot. Read them from flash with `tools/boot_log.py` in download mode when serial lost lines or the boot can't have a logger attached (OTA trial boots). Fields in [`serial-diagnostics.md`](references/features/serial-diagnostics.md).
- **Design:** `tools/weather_design.py` writes `src/weather/weather_fonts.h`, `src/weather/weather_words.h`, `tools/preview.png` and `tools/preview_words.png`, and prints the worst-case gaps.
- **Crash decode:** `~/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-addr2line -pfiaC -e .pio/build/<env>/firmware.elf <addrs>`. Check the `ELF file SHA256:` prefix in the panic against `shasum -a 256 firmware.elf` first. A different build decodes to the wrong lines.

## Ship an update

Devices update themselves from GitHub releases (design in [`ota-updates.md`](references/features/ota-updates.md)). Releases come only from the pipeline, never flashed from a laptop. **A tag publishes to every device, so get the user's OK before pushing one.**

1. **Prove the change first** (proof bar below). If it touches networking, OTA, boot, sleep or the partition layout, also run the staging rig in `ota-updates.md`.
2. **Pick the version:** the next semver after `git describe --tags --match 'weather-v*' --abbrev=0`. Patch for fixes, minor for features or new screens, major for anything that changes what a device must already have. Add `-rc.N` (e.g. `weather-v1.2.0-rc.1`) to ship to the staging channel only.
3. **Commit as the user.** The author must be `willbraden <willbraden@gmail.com>` and nobody else: no `Co-Authored-By` trailers in this repo (the user's rule).
4. **Push and tag.** The remote is named `willbraden`, not `origin`:
   ```bash
   git push willbraden main
   git tag -a weather-v1.0.1 -m "pebl weather 1.0.1: <one-line summary>"
   git push willbraden weather-v1.0.1
   ```
   **From a cloud session** (its GitHub connection drops tag pushes: git prints `Everything up-to-date` and no tag appears): push `main`, then run the workflow on `main` with the version instead of tagging. With the GitHub tools that is `actions_run_trigger` `run_workflow`, `workflow_id: weather-release.yml`, `ref: main`, `inputs: {"version": "1.0.1"}`. The run refuses a version that already exists or isn't newer than the latest release, then builds, signs and verifies, and only then creates the tag on that commit. A failed run leaves no tag behind.
5. **Watch the pipeline** (about 4 minutes): build, unit tests, sign, self-verify, publish, update the channel manifest. If `gh` isn't allowed in the session, skip to step 6 and poll the channel manifest until it shows the new version.
   ```bash
   gh run list --repo willbraden/esp32 --workflow weather-release.yml --limit 1
   gh run watch <run-id> --repo willbraden/esp32 --exit-status
   ```
6. **Check what was published**, independently of CI:
   ```bash
   curl -sL https://github.com/willbraden/esp32/releases/download/weather-ota-stable/manifest.json
   curl -sLO https://github.com/willbraden/esp32/releases/download/weather-v1.0.1/pebl-weather-1.0.1.bin
   curl -sL -o manifest.json https://github.com/willbraden/esp32/releases/download/weather-ota-stable/manifest.json
   ~/.platformio/penv/bin/python tools/ota_sign.py verify --firmware pebl-weather-1.0.1.bin --manifest manifest.json
   ```
   (Use `weather-ota-staging` for an `-rc` tag.)
7. **Prove it on the device.** Ask the user to hold USER SW about 2 s (or wait up to 2 h). The panel shows "Updating firmware... vOLD -> vNEW", then the weather again. Ask what the panel shows after the update: the weather, not a blank or status screen. **Don't attach a logger during the update**: a reset in the trial boot rolls it back. Afterwards, put the board in download mode and read `tools/boot_log.py`. Expect the install boot (`ota=installing-1.0.1 draw=...,msg:~2300ms next-boot=sw-restart`), then the trial boot (`v1.0.1 ... >drawn>ota>sleep ota=up-to-date trial draw=numbers:~2300ms`), and no `try_ver` in the OTA state (it was confirmed). `rolled-back-from=1.0.1(attempt-n/3)` means the update failed its trial boot; the device retries up to 3 times. Start the board again with `--after watchdog_reset`.

Never:
- Move, delete or reuse a published `weather-v*` tag. Devices never downgrade, so a bad release is fixed by releasing a higher version.
- Edit the `weather-ota-stable` / `weather-ota-staging` channel releases by hand. The workflow owns them.
- Commit, print or paste the private key (`~/.pebl/ota_signing_key.pem`, the `PEBL_OTA_SIGNING_KEY` secret). The user keeps a backup in their password manager.

What OTA can't deliver: a new partition table, bootloader, or flash layout. Those need a USB flash of every device. Rotating the signing key is possible over the air: ship one release signed with the **old** key that carries the **new** `ota_pubkey.h`, then replace the secret. Devices that miss that release need USB.

## Proof bar

A claim about the device needs device evidence. "It compiled" and "the preview looks right" are not proof of on-device behavior.

- Show the serial lines that prove the path ran. The firmware prints a stable marker for each step, listed in [`serial-diagnostics.md`](references/features/serial-diagnostics.md): wake cause, mode, cache vs fetch, the reading, `_Update_Full`, `Sleeping`.
- For anything visual, show the design-tool preview and ask the user for a photo of the panel. The preview proves the pixels; the photo proves the panel actually refreshed. The boot log's `draw=<screen>:<ms>` proves a refresh ran (~2300 ms) on boots nobody watched, but not what it showed.
- Exercise every path the change touches: cold boot, timer wake, USER SW wake (cached and stale cache), WiFi failure, fetch failure, and both screens. The feature file lists the paths.
- When serial output stops early, the boot log is the evidence: a record whose path ends `>sleep` finished; `next-boot=PANIC` means it crashed at the end of its path.
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
