# Flashing and recovery

Getting a build onto the board whether it's awake, asleep, or crash-looping.

## Sub-features

- flash-awake: auto-reset into the bootloader over USB when firmware is running
- flash-asleep: BOOT/RESET sequence when the weather firmware is in deep sleep
- leave-download-mode: start the new firmware without touching the board
- recover-crash-loop: flash a board whose firmware keeps crashing
- port-names: which `/dev/cu.usbmodem*` to use

## How to get to it (user POV)

Download mode, which the user does by hand: **hold BOOT, tap RESET, release BOOT.** The port appears and stays. If BOOT is still held when RESET is released and again at the next reset, the chip keeps re-entering download mode (`boot:0x0 ... waiting for download`). That's the most common "it didn't boot" cause.

## Driving it

```bash
PORT=$(ls /dev/cu.usbmodem* | head -1)
~/.platformio/penv/bin/pio run -e pebl_weather -t upload --upload-port $PORT
# chip was put in download mode by hand -> start the app:
~/.platformio/penv/bin/python ~/.platformio/packages/tool-esptoolpy/esptool.py \
    --chip esp32s3 --port $PORT --after watchdog_reset chip_id
```

Evidence of a good flash: `[SUCCESS]` from pio, then on serial `rst:... boot:0x8 (SPI_FAST_FLASH_BOOT)` followed by `=== pebl weather ===`.

## Gotchas

- `--after hard_reset` (pio's default) does not leave download mode when it was entered by the BOOT strap. Use `watchdog_reset` or a RESET tap.
- The port name changes with the USB socket: `/dev/cu.usbmodem101` on one, `/dev/cu.usbmodem1101` on another. `platformio.ini` hardcodes 101; always pass `--upload-port`.
- "port is busy or doesn't exist": a logger still has it open. `lsof /dev/cu.usbmodem*`, kill it, retry.
- No port at all and `ioreg -p IOUSB` shows no ESP device: a charge-only cable, or the board is asleep. Swap the cable before anything else.
- The platform is pinned to pioarduino **55.03.311** (arduino-esp32 3.3.11, IDF 5.5.5) in `platformio.ini`. It needs PlatformIO Core ≥ 6.1.19 (`~/.platformio/penv/bin/pip install platformio==6.1.19`); 55.03.312 needs ≥ 6.2.0. Older platforms crash more often; see `known-issues.md`.
- The board's flash is 4 MB. The `esp32-s3-devkitc-1` profile defaults to 8 MB and writes a header that aborts at boot. Every pebl env sets `board_upload.flash_size = 4MB` and `board_build.flash_size = 4MB`. Keep them.
- Crash-looping firmware kills USB every few seconds. Use the BOOT sequence, not auto-reset, and don't open the serial port while recovering.
