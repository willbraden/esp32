# pebl weather

Firmware that turns a pebl.ink ESP32-S3 e-ink board into a battery-powered weather
display. It wakes every 30 minutes, shows today's weather on the 2.13" panel, and goes
back to deep sleep. It needs no account, no API key and no computer once it's flashed.

- **Location:** found automatically from the network's IP address (ip-api.com)
- **Weather:** current temperature, today's high/low and conditions from Open-Meteo
- **Two screens:** numbers (big Hi/Lo, current temp, conditions, an inverted weekday bar
  that shows how old the reading is) or one word (SNOW / RAIN / FREEZE / COLD / CHILLY /
  sunglasses / HOT / HELL)
- **USER SW button:** press to switch screens (saved across power-off). Hold ~2 s to
  fetch fresh weather and check for an update.
- **Updates itself** over WiFi from signed GitHub releases, with automatic rollback if a
  new version fails to boot

## Hardware

ESP32-S3-WROOM-1 (4 MB flash, native USB), SSD1680 2.13" black/white panel driven at
250 × 122, LiPo battery. There is no battery-voltage sensing on this board.

| Signal | GPIO |
|---|---|
| Display PWR / RST / CS / DC / BUSY | 13 / 8 / 16 / 12 / 14 |
| Display SCLK / MOSI | 10 / 9 |
| USER SW (shared with display CS) | 16 |
| BOOT | 0 |

How the pins were found: [EINK_PIN_DISCOVERY.md](EINK_PIN_DISCOVERY.md).

## Build and flash

Needs PlatformIO Core 6.1.19 or later.

```bash
pio run -e pebl_weather                                   # build
pio run -e pebl_weather -t upload --upload-port /dev/cu.usbmodem101
python tools/serial_log.py --quiet --until Sleeping       # watch one wake cycle
```

The USB port disappears while the board sleeps. To flash it then, hold BOOT, tap RESET,
release BOOT, and upload. After flashing in download mode, tap RESET once (or run
`esptool.py --chip esp32s3 --port <port> --after watchdog_reset chip_id`).

First boot with no saved network opens a `pebl-weather` WiFi hotspot. Join it from a
phone and pick your network.

## Releasing an update

Releases come only from CI. Push an annotated tag and the
[release workflow](.github/workflows/weather-release.yml) builds, tests, signs and
publishes it. Devices pick it up within about 2 hours.

```bash
git tag -a weather-v1.0.1 -m "pebl weather 1.0.1: <summary>"
git push willbraden weather-v1.0.1        # -rc tags go to the staging channel only
```

The signing key is never in the repo. CI reads it from the `PEBL_OTA_SIGNING_KEY`
secret. Full procedure and safeguards:
[`.claude/skills/verify-pebl/SKILL.md`](.claude/skills/verify-pebl/SKILL.md#ship-an-update).

## Layout

| Path | What |
|---|---|
| `src/weather/` | The firmware: `weather_main.cpp`, OTA updater (`ota.cpp`, `semver.h`), generated fonts and word art |
| `tools/weather_design.py` | Layout tool: generates the fonts and word art, and renders pixel-exact previews |
| `tools/ota_sign.py` | Signs and verifies release manifests |
| `tools/serial_log.py` | Serial logger that survives deep sleep |
| `tools/test_semver.cpp` | Unit tests for version comparison |
| `src/battprobe/`, `src/btnprobe/`, `src/netstress/`, `src/otafail/` | Diagnostic firmwares, one env each |
| `src/wifi/`, `src/config/`, `src/main.cpp`, `data/` | WiFi tooling from the original pebl Slack client, kept as reference. Not built |
| `.claude/skills/verify-pebl/` | Full feature and hardware reference, plus how to prove changes on the device |

## Known issues

An lwIP `pbuf_free` crash hits about 1 in 3 boots right after the location lookup (it reboots
and tries again, so the screen still updates), and a rare wedge after a software reboot
leaves the board frozen until it's power-cycled. Details: [known-issues.md](.claude/skills/verify-pebl/references/features/known-issues.md).
