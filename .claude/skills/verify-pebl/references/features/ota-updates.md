# OTA updates

The device updates itself from signed GitHub releases. Code: `src/weather/ota.{h,cpp}`, `src/weather/semver.h`, `tools/ota_sign.py`, `.github/workflows/weather-release.yml`.

## Sub-features

- pull-check: after a successful fetch + render, fetch the channel manifest every `OTA_CHECK_EVERY_N_WAKES` (4 → ~2 h), on every power-on or RESET, but not on the boot right after a rollback (the next try waits for the normal schedule), and on a USER SW long press (still held 1.5 s after the app starts)
- channels: `weather-ota-stable` (plain versions) and `weather-ota-staging` (also `-rc` prereleases), set at build time by `OTA_CHANNEL_URL`
- version gate: install only if the manifest version is newer (`semver.h`, unit-tested by `tools/test_semver.cpp`); local builds are `X.Y.Z+dev...`, which equals its tag
- download: HTTPS with certificate validation (embedded Mozilla bundle), following GitHub's redirect, streamed into the spare app slot, hashed as it arrives
- signature: ECDSA P-256 over `pebl-weather-ota/1\n<version>\n<size>\n<sha256>\n`, checked against `ota_pubkey.h` **before** the boot partition changes
- trial boot + rollback: the new image boots `PENDING_VERIFY` and confirms only after a successful fetch + render (`ota::confirmHealthy()`). A crash or reset before that makes the bootloader roll back. **A deep-sleep wake does not** (`CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP` re-enters the last image without reading the OTA state), so the unconfirmed image counts its own failed boots before each sleep (`trial_n`) and calls `esp_ota_mark_app_invalid_rollback_and_reboot()` after `MAX_TRIAL_BOOTS` (3: the install restart plus 2 wakes, ~1 h in production). Trial boots are unattended (no setup portal), skip the cached redraw, and retry Wi-Fi 3× and each fetch 2×
- retry limit: a version that rolls back 3 times is skipped for good (`pebl-ota` NVS: `bad_ver`, `bad_cnt`); a confirmed version clears its own history
- status screen: "Updating firmware... vA -> vB" while downloading

Partition table (`partitions.csv`): two 1,984 KB app slots (`app0`, `app1`) + `otadata`. The firmware is ~1.45 MB.

## How to get to it (user POV)

Ship an update: follow **Ship an update** in [`SKILL.md`](../../SKILL.md) (version choice, annotated tag pushed to the `willbraden` remote, watching CI, verifying the published files, proving it on the device). Short form:

```bash
git push willbraden main
git tag -a weather-v1.2.0 -m "pebl weather 1.2.0: <summary>" && git push willbraden weather-v1.2.0
```

The device picks it up within ~2 hours. Hold USER SW ~2 s to check now. Use `weather-v1.3.0-rc.1` to send to staging devices only.

Releases so far (all 2026-10-13):

| Tag | Change | Over the air |
|---|---|---|
| `weather-v1.0.0` | first live release | updated from 0.9.0 (long press), confirmed |
| `weather-v1.1.0` | JetBrains Mono fonts | rolled back 3× by the `pbuf_free` crash, skipped; delivered by USB |
| `weather-v1.1.1` | crash fix, OTA trial hardening, boot log | confirmed first time; that first draw was blank (`known-issues.md`) |
| `weather-v1.1.2` | panel refresh timing in the boot log | confirmed first time on battery, weather drawn |

One-time setup: the repo secret `PEBL_OTA_SIGNING_KEY` must hold `~/.pebl/ota_signing_key.pem` (`gh secret set PEBL_OTA_SIGNING_KEY --repo willbraden/esp32 < ~/.pebl/ota_signing_key.pem`). Back the key up somewhere safe: without it, devices can only be updated over USB.

## Driving it

Staging rig (verified 2026-10-12, all five passed):

```bash
export PEBL_OTA_STAGING_URL=http://<mac-ip>:8765/manifest.json
PEBL_FW_VERSION=1.0.0 pio run -e pebl_weather_staging     # wakes every minute, checks every wake
# flash it (bootloader, partitions, boot_app0 at 0xe000, app at 0x10000)
python -m http.server 8765 --bind 0.0.0.0                 # in the folder holding manifest.json + .bin
python tools/ota_sign.py sign --firmware fw.bin --version 1.0.1 --url http://<mac-ip>:8765/fw.bin \
    --key ~/.pebl/ota_signing_key.pem --out manifest.json
```

| Case | Expected serial |
|---|---|
| same version | `OTA: up to date (running v1.0.0, channel v1.0.0)` |
| valid newer | `OTA: installing ...`, `verified and installed, rebooting`, `first boot of v1.0.1 ... (trial)`, `confirmed healthy` |
| byte changed after signing | `OTA: sha256 mismatch` (stays on old version) |
| signed with another key | `OTA: signature INVALID, rejecting image` |
| image never confirms (`pebl_otafail`) | `never confirmed, rolled back ... (attempt n/3)` ×3, then `OTA: skipping v9.9.9 (rolled back 3 times)` |

The Python server's access log is the most reliable evidence of what was downloaded and when. Serial loses lines around the reboots.

Watching a trial boot's screen (the blank-screen tests, 2026-10-13): build two `pebl_weather_staging` images with fresh versions (e.g. 1.3.0 and 1.3.1), flash the first over USB, and leave `manifest.json` out of the served folder until the first boot is logged (it reports `ota=no-channel`). Sign the second into a held-back manifest and copy it into place only when the user is ready (and unplugged, for a battery test). The next 1-minute wake installs it; the server log shows the `.bin` download and then the trial boot's manifest check. Ask what the panel shows, then read the boot log. For a download as slow as GitHub's, serve the `.bin` from `tools/ota_slow_server.py` (`--port 8766 --rate 24000`, ~70 s) and sign the manifest with that URL. When done, flash `pebl_weather` back over USB: a staging build only updates from the Mac.

Fault injection (proves retry, no-portal and self-rollback; run it after any change to the trial-boot path):

```bash
export PEBL_OTA_STAGING_URL=http://<mac-ip>:8765/manifest.json
PEBL_FW_VERSION=1.2.0 PEBL_FAULT_WIFI_FAILS=0 pio run -e pebl_weather_fault   # base: flash over USB
PEBL_FW_VERSION=1.2.1 PEBL_FAULT_WIFI_FAILS=1 pio run -e pebl_weather_fault   # serve: must confirm after one retry
PEBL_FW_VERSION=1.2.2 PEBL_FAULT_WIFI_FAILS=3 pio run -e pebl_weather_fault   # serve: must roll back 3x, then be skipped
```

Copy each `firmware.bin` out of `.pio/build/pebl_weather_fault/` before the next build, sign each served one, and swap `manifest.json` between stages. Track the server log by device IP and line number (don't write markers into a file `tee` is writing). Then read the boot log with `tools/boot_log.py`: trial boots show `trial`, `fail=wifi`, `notes=...wifi-retry-...,trial-failed-n/3,self-rollback`; rollback boots show `rolled-back-from=`. Use fresh version numbers each run, because leftover `pebl-ota` state from an earlier run is kept in NVS.

Production check after a real release: `curl -sL https://github.com/willbraden/esp32/releases/download/weather-ota-stable/manifest.json` shows the version, then the device logs `OTA: up to date (running vX, channel vX)` on its next check.

## Gotchas

- Any crash during a trial boot rolls a good update back. Until 2026-10-13 the `pbuf_free` crash (`known-issues.md`, now fixed) did that on most boots: v1.0.1 needed 2 attempts in the staging test, and `weather-v1.1.0` used all 3 and was skipped ("OTA updates fail their trial boot").
- After a failed update, read the board's boot log and OTA state with `tools/boot_log.py` (download mode). Trial boots run without a logger, so this is the only record of why they failed.
- A logger that reconnects during the trial boot resets the chip, which also rolls the update back. Watch the server log instead, or attach after the confirm.
- USB-flashing writes `boot_app0.bin` to `otadata`, which resets the boot choice to `app0`. Writing only the app at 0x10000 after an OTA leaves the device booting `app1`, the old image.
- Never commit the private key. `ota_pubkey.h` is public on purpose.
- Rotating the key needs one USB flash per device with a build carrying the new `ota_pubkey.h`.
- `verifyRollbackLater()` is overridden in `ota.cpp`. Without it the Arduino core confirms every image at boot, and rollback never happens.
