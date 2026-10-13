# Known issues

Open bugs with their evidence, and resolved ones that could come back.

## Sub-features

### OPEN: lwIP `pbuf_free: p->ref > 0` crash, mostly on the first requests after a boot

**Not fixed.** The platform upgrade to 55.03.311 (arduino-esp32 3.3.11, IDF 5.5.5) removed it from steady-state loops (0 in 90 requests in one boot, vs 10 in 55 on the old core) but the weather firmware still crashed on roughly 1 in 3 boots on the new core (2026-10-12, staging OTA test, 1-minute wakes). Each crash costs a reboot; the screen still updates. **It matters for OTA:** a crash during an update's trial boot rolls the update back (hence 3 attempts before a version is skipped).

Ruled out so far: TLS (HTTPS alone never crashed), `HTTPClient` socket reuse, PSRAM buffer placement (WiFi uses static copy buffers), source-port reuse across reboots (ports are already random: 59079, 62551, 53983, ...). Next experiment: one round per boot (`pebl_netstress` v2), varying what runs first after WiFi connects.

History:

- Symptom: `assert failed: pbuf_free ... pbuf.c:755 (pbuf_free: p->ref > 0)`, then `Rebooting...`. In the weather firmware it fires right after `Loc:`. Up to 3 crashes in a row were seen before a boot got through.
- Decoded stack (same both times): `tcpip_thread → ethernet_input → ip4_input → tcp_input → tcp_process → tcp_receive → tcp_free_acked_segments → tcp_seg_free → pbuf_free`. lwIP's own thread frees a sent segment that was already freed.
- Isolation (`pebl_netstress`, arduino-esp32 3.3.2 / ESP-IDF 5.5.1): plain HTTP to ip-api 4/20 crashed, HTTPS to Open-Meteo 0/20, both in sequence 6/15. TLS is not the trigger; the plain `WiFiClient` path is.
- Ruled out: `HTTPClient` socket reuse. With `http.setReuse(false)` (`HTTP_NO_REUSE 1` in netstress) the second request still crashed.
- Core version: the old core crashed in steady state too; 3.3.11 / IDF 5.5.5 ran 90 of 90 in one boot. Keep the newer platform, but it did not fix the after-boot crash.
- Also seen once on the old core: after the 11th crash in a stress run, USB stopped answering auto-reset (the red LED was off; replugging fixed it). Needed the BOOT sequence.

### OPEN: board occasionally wedges after a software reboot

Seen twice in ~150 reboots on 2026-10-12, both right after `rst:0xc (RTC_SW_CPU_RST)` (a panic reboot and an `ESP.restart()`). The ROM line prints, then nothing; USB still enumerates but esptool gets "No serial data received". The red LED was off the first time. Only a USB replug brought it back. Cause unknown. If it hits on battery, the device stays frozen until power is cycled. OTA installs end in `ESP.restart()`, so this needs fixing before relying on unattended updates.

### RESOLVED: double full refresh on every wake

`display.init(..., initial=true, ...)` made GxEPD2 blank the panel with its own full refresh before ours. Fixed with `initial=false` (every update here is full-window, which writes both buffers). Check: exactly one `_Update_Full` per render.

### RESOLVED: earlier flashing and boot failures

8 MB flash header on a 4 MB chip (boot loop), TLS handshake overflowing the 8 KB loop stack (`-D ARDUINO_LOOP_STACK_SIZE=16384`), and WiFiManager's portal blocking unattended wakes. See `flashing-and-recovery.md` and `wifi-and-provisioning.md`.

## How to get to it (user POV)

The crash shows up only as a slower update (extra reboots before the screen changes). The wedge shows up as a screen that stops updating, same as a dead battery.

## Driving it

- Regression check after any platform or network-code change: flash `pebl_netstress`, attach one logger for the whole run, and require 0 crashes in all three phases (30 rounds each). The old baseline was ~18% per HTTP request.
- Then a clean `pebl_weather` boot log with no `assert failed` / `Rebooting...`.

## Gotchas

- Decode backtraces only against the ELF whose SHA prefix matches the panic. Rebuilding with any change shifts the addresses.
- A crash that recovers still shows the right screen. Search the log for it; don't infer it from the panel.
