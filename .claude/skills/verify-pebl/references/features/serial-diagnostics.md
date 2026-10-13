# Serial diagnostics

The serial log is the main evidence channel. This lists every marker the weather firmware prints and how to read resets and crashes.

## Sub-features

- logger: `tools/serial_log.py`, which survives deep sleep and opens without resetting
- boot-markers: one line per step of a wake cycle
- reset-reasons: telling a real wake from a USB-induced reset or a crash
- crash-decode: turning a panic backtrace into source lines
- boot-log: a persistent record of each boot in NVS (`src/weather/boot_log.cpp`), printed at boot and readable from flash with `tools/boot_log.py`. **The ground truth when serial drops lines**

## How to get to it (user POV)

Not user-facing. The user only sees the panel. The log is for verification.

## Driving it

```bash
~/.platformio/penv/bin/python tools/serial_log.py --quiet --until Sleeping --timeout 90
```

Markers in order for one wake (`src/weather/weather_main.cpp`):

| Line | Meaning |
|---|---|
| `rst:0x.. boot:0x8` | ROM reset reason, then a normal flash boot (`boot:0x0` = stuck in download mode) |
| `=== pebl weather ===` | App started |
| `Boot log: #N v... path=... next-boot=...` | The last 4 archived boots (see Boot log below) |
| `Boot #N  wake=W (TIMER WAKE / USER SW / power-on/reset)` | Wake cause. N counts within one power-on (RTC memory) |
| `Mode M` | 0 = numbers, 1 = word, after any toggle |
| `Redrawing cached reading (Ns old)` | USER SW path used the RTC cache, with no WiFi |
| `WiFi OK: <ip>` | Saved-credential reconnect worked |
| `Loc: <city> (<lat>, <lon>)` | ip-api lookup worked |
| `Temp T  Hi H  Lo L  code C  precip6h P% [(snow)]  @ DAY time` | The reading every screen is drawn from |
| `_Update_Full : <µs>` | One full panel refresh (~2.3 s). **Exactly one per render** |
| `Sleeping Ns (or until USER SW)...` | Entering deep sleep; the port drops right after |

Reset reasons:

- `rst:0x15 (USB_UART_CHIP_RESET)`: reset from the USB side, by esptool or by a host reopening the port.
- `rst:0xc (RTC_SW_CPU_RST)` after `Rebooting...`: a crash (panic) reboot.
- `rst:0x5 (DSLEEP)`: a genuine wake from deep sleep.

Boot log (one line per boot, last 8 kept in NVS `pebl-boot`):

```
#6 v1.1.0 wake=reset path=boot>wifi>loc up=13.5s wifi=12635ms next-boot=PANIC
#7 v1.1.0 wake=reset path=boot>wifi>loc>weather>drawn>ota>sleep up=15.0s wifi=8007ms ota=up-to-date
```

| Field | Meaning |
|---|---|
| `#N` | Boot sequence number. Never resets (survives power-off), so it orders records. There's no wall clock |
| `wake` | `timer`, `button` or `reset` (anything that isn't a deep-sleep wake) |
| `path` | Stages passed, in order: `boot>wifi>loc>weather>drawn>ota>sleep`, or `>cache>sleep` for a button redraw. The last one is how far the boot got |
| `up` | Uptime at the last stage |
| `wifi` | Time spent on the direct reconnect (25 s = timed out) |
| `fail` | `wifi`, `wifi+portal`, `wifi+portal-timeout`, `loc`, `weather` |
| `ota` | Update check result: `up-to-date`, `no-channel`, `skipped-bad`, `failed`, `installing-<ver>` |
| `trial`, `rolled-back-from=<ver>(attempt-n/3)` | First boot of an OTA update; the boot right after a rollback |
| `next-boot` | Reset reason of the following boot. `PANIC` / `*-WDT` = this boot crashed at the end of its path; `sw-restart` = OTA install restart; `deep-sleep` = normal wake; `power-on` = RESET or power cycle; `usb-reset` = esptool or a logger reopening the port. Missing = no boot since |

Read it without serial: put the board in download mode and run `tools/boot_log.py` (or `--dump nvs.bin`). It also prints the OTA rollback state (`pebl-ota`).

Crash decode: check the panic's `ELF file SHA256:` prefix against `shasum -a 256 .pio/build/<env>/firmware.elf | cut -c1-9`, then run `xtensa-esp32s3-elf-addr2line -pfiaC -e <elf> <addresses>`.

## Gotchas

- If the logger reconnects after a deep-sleep wake, the reopen can reset the chip, and the log shows `power-on/reset` instead of the real cause. That cost an hour on the USER SW test. Verify button wakes on screen.
- **OPEN: everything after the panel refresh can be lost.** On 2026-10-13, 4 boots printed nothing after `_Update_Full` (no `OTA:`, no `Sleeping`) and the port vanished. The boot log of one of them (#7) shows it finished normally (`...>drawn>ota>sleep`, `ota=up-to-date`), so the firmware ran and only the console output was lost. Other boots the same morning printed those lines fine. Cause unknown. Trust the boot log over a log that stops early.
- `E (…) esp_core_dump_flash: No core dump partition found!` on every boot is expected (the partition table has none). It isn't an error in our code.
- `nvs_open failed: NOT_FOUND` used to appear on the first boot after a fresh flash. It was fixed by opening Preferences read-write. If it's back, `loadMode()` regressed.
