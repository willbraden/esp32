# Diagnostic firmwares

Standalone probe builds, each with its own env and source folder. Use them to answer hardware questions instead of guessing. Flashing one replaces the weather firmware until you flash `pebl_weather` back.

## Sub-features

| Env | Source | Answers |
|---|---|---|
| `pebl_battprobe` | `src/battprobe/` | Battery sense. The committed version (v5) is a fast-drain monitor that shows GPIO18 on the panel every ~10 s. The earlier ADC/pull/enable-line/I2C scans were never committed; their results are in `power-and-battery.md` |
| `pebl_btnprobe` | `src/btnprobe/` | Which pin a button is on. Samples every pin under pull-up **and** pull-down each loop, including display pins, and prints `Gn: float -> GND` style changes |
| `pebl_netstress` | `src/netstress/` | Network crash isolation with a verbatim copy of `httpGet()`, counting crashes in `RTC_NOINIT` memory across reboots. v2 (current) does one round per boot then `ESP.restart()`, like an OTA trial boot, and logs each boot's source port. v1 (git history) looped many requests in one boot |
| `pebl_otafail` | `src/otafail/` | OTA rollback test image: never confirms itself, reboots after 5 s. Ship it signed through the staging channel to prove rollback |

## How to get to it (user POV)

Not user-facing. The user presses buttons or unplugs USB when a probe asks for it.

## Driving it

```bash
~/.platformio/penv/bin/pio run -e pebl_btnprobe -t upload --upload-port $PORT
~/.platformio/penv/bin/python tools/serial_log.py --quiet --timeout 300
# ask the user to press USER SW a few times, then BOOT once
```

Expected btnprobe result: `G16: 3V3 -> GND` per USER SW press, `G0: float -> GND` for BOOT.

netstress results (2026-10-12):

| Phase | 3.3.2 / IDF 5.5.1 | 3.3.11 / IDF 5.5.5 (current) |
|---|---|---|
| HTTP only (ip-api) | 16 ok, 4 crashes | 30 ok, 0 |
| HTTPS only (Open-Meteo) | 20 ok, 0 | 30 ok, 0 |
| HTTP → HTTPS | 9 ok, 6 crashes (cut short) | 30 ok, 0 |

`HTTP_NO_REUSE` (default 0) is the reuse experiment that ruled out `HTTPClient` socket reuse.

## Gotchas

- None of these deep-sleep, so the port stays up and `-t upload` auto-resets them. No BOOT sequence needed while a probe is running.
- Probes don't save state to NVS, except that WiFi credentials are untouched. Flashing `pebl_weather` back restores normal behavior, and the saved mode is kept.
- netstress resets its counters on any non-panic boot (RESET, power-on, or a USB reset from reopening the port). Leave one logger attached for the whole run.
