# pebl feature map

Behavior-level inventory of the pebl weather display (env `pebl_weather`, source `src/weather/weather_main.cpp`) and the board it runs on. Agents use this to decide what to drive and what counts as evidence. Humans use it as the regression checklist.

_Last verified on hardware: 2026-10-12._

## Baseline preconditions

- Board on USB with a **data** cable. A charge-only cable shows no port and no USB devices in `ioreg -p IOUSB`.
- Power switch ON. The board has a battery, so unplugging USB does not stop or reset it.
- Saved WiFi credentials on the device (provisioned once through the `pebl-weather` portal).
- No other process holding the serial port (`lsof /dev/cu.usbmodem*`).
- Fresh build of the env you are proving, flashed in this session.

## Proof and skip reporting

- Exercise every path the change can affect: cold boot, timer wake, USER SW wake, the error screens, and both display modes.
- Record the trigger and the end state: serial lines for the path, plus a panel photo for anything visual.
- The design-tool preview stands in for the panel only for pixel layout. It does not prove the panel refreshed.
- When a path is unreachable (for example a 95°F day or real snow), say so, and prove it the closest real way: the word rules are a pure function of the reading, so the preview sheet covers every word.

## Full sweep

Walk this list top to bottom, then `multi-surface-journeys.md`.

## Board

- [hardware-and-pins](hardware-and-pins.md): MCU, display, full GPIO map, buttons, power, unidentified parts.
- [power-and-battery](power-and-battery.md): Battery behavior, why there is no battery reading, the divider that would add one.

## Getting code onto it

- [flashing-and-recovery](flashing-and-recovery.md): Flash while awake or asleep, BOOT/RESET sequence, leaving download mode, port names.
- [serial-diagnostics](serial-diagnostics.md): The logger, every serial marker the firmware prints, crash decoding, reset reasons.

## Weather firmware

- [wake-and-sleep-cycle](wake-and-sleep-cycle.md): Wake causes, the 30-minute schedule, the RTC cache, deep sleep.
- [wifi-and-provisioning](wifi-and-provisioning.md): Saved-credential reconnect, the `pebl-weather` portal, failure behavior per wake cause.
- [weather-data](weather-data.md): IP geolocation and the Open-Meteo request: fields, units, the 6-hour precipitation window.
- [night-mode](night-mode.md): White-on-black screens between sunset and sunrise, driven by `is_day`.
- [tomorrow-screen](tomorrow-screen.md): "Tomorrow" over the next day's low-high, in place of both screens at night.
- [numbers-screen](numbers-screen.md): High/low, big current temp, condition word, layout and worst-case gaps.
- [word-screen](word-screen.md): One-word screen (SNOW/RAIN/FREEZE/COLD/CHILLY/sunglasses/HOT/HELL) and its rules.
- [weekday-bar](weekday-bar.md): Inverted weekday strip, the stale-screen cue shared by both screens.
- [mode-button](mode-button.md): USER SW on GPIO16, wake-on-press, mode toggle and NVS persistence.
- [status-screens](status-screens.md): WiFi, location and weather error screens.
- [ota-updates](ota-updates.md): Signed over-the-air updates from GitHub releases: channels, trial boot, rollback, staging rig.

## Tooling

- [design-tool](design-tool.md): `tools/weather_design.py`: fonts, word art, previews, gap check.
- [diagnostic-firmwares](diagnostic-firmwares.md): `pebl_battprobe`, `pebl_btnprobe`, `pebl_netstress`, `pebl_otafail`: what each one proves.

## Open problems and legacy

- [known-issues](known-issues.md): Open bugs with evidence (the USB link dying, OTA trial boots failing) and fixed ones that could come back (the `pbuf_free` crash).
- [legacy-wifi-tooling](legacy-wifi-tooling.md): The original client's multi-network WiFi and QR provisioning code, kept unbuilt as reference, and what a port must change.

## Multi-surface journeys

- [multi-surface-journeys](multi-surface-journeys.md): Full day-in-the-life runs that cross wake, mode, cache and power.

## Entry contract

Every feature file uses the same four H2s:

1. `Sub-features`
2. `How to get to it (user POV)`
3. `Driving it`
4. `Gotchas`
