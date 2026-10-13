# Serial diagnostics

The serial log is the main evidence channel. This lists every marker the weather firmware prints and how to read resets and crashes.

## Sub-features

- logger: `tools/serial_log.py`, which survives deep sleep and opens without resetting
- boot-markers: one line per step of a wake cycle
- reset-reasons: telling a real wake from a USB-induced reset or a crash
- crash-decode: turning a panic backtrace into source lines

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

Crash decode: check the panic's `ELF file SHA256:` prefix against `shasum -a 256 .pio/build/<env>/firmware.elf | cut -c1-9`, then run `xtensa-esp32s3-elf-addr2line -pfiaC -e <elf> <addresses>`.

## Gotchas

- If the logger reconnects after a deep-sleep wake, the reopen can reset the chip, and the log shows `power-on/reset` instead of the real cause. That cost an hour on the USER SW test. Verify button wakes on screen.
- Lines printed in the last few ms before deep sleep can be lost; the USB port drops mid-flush.
- `E (…) esp_core_dump_flash: No core dump partition found!` on every boot is expected (the partition table has none). It isn't an error in our code.
- `nvs_open failed: NOT_FOUND` used to appear on the first boot after a fresh flash. It was fixed by opening Preferences read-write. If it's back, `loadMode()` regressed.
