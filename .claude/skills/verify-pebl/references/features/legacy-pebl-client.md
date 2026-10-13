# Legacy pebl client

The repo started as the pebl Slack/Discord reactions client for LilyGo T5 boards (`src/main.cpp`, `src/config/`, `src/ota/`, `src/resilience/`, `src/security/`, `src/wifi/`, `flash.sh`, `data/config.json.example`). It is still built by the `lilygo_*` envs. The weather firmware doesn't use any of it.

## Sub-features

- WebSocket (WSS) client with encryption and lock/unlock indicator
- emoji PNG rendering, SPIFFS/LittleFS JSON config, OTA, resilience/backoff
- battery indicator (broken on this board, see Gotchas)

## How to get to it (user POV)

`./flash.sh` (interactive) or `pio run -e lilygo_t5_s3_depg_bw -t upload`. It needs `data/config.json` (gitignored) and a pebl server account.

## Driving it

Unverified on this board. If you need it: env `lilygo_t5_s3_depg_bw` targets the S3, but its display construction must use the pebl pins (PWR 13, RST 8, CS 16, DC 12, BUSY 14, SCLK 10, MOSI 9) and the 4 MB flash settings from `pebl_weather`.

## Gotchas

- `getBatteryStatus()` (around `src/main.cpp:1962`) reads `ADC1_CHANNEL_7` / `ADC1_CHANNEL_0` assuming classic-ESP32 pins 35/36. On the S3 those are different GPIOs, and this board has no battery divider anyway (see `power-and-battery.md`).
- The `lilygo_t5_s3_*` envs don't set the 4 MB flash size, so they will boot-loop with the 8 MB header on this board.
- The global `[env]` `build_src_filter` excludes `weather/`, `battprobe/`, `btnprobe/` and `netstress/`. Add new standalone apps to that list, or the legacy build will pick up a second `setup()`.
