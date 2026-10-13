# Known issues

Open bugs with their evidence, and resolved ones that could come back.

## Sub-features

### RESOLVED 2026-10-12: lwIP `pbuf_free: p->ref > 0` crash on the plain-HTTP request

Fixed by moving the platform from pioarduino 55.03.32 (arduino-esp32 3.3.2, ESP-IDF 5.5.1) to 55.03.311 (3.3.11, IDF 5.5.5). Same stress code, before vs after: 10 crashes in 55 HTTP-involving requests vs 0 in 90 (30 per phase). A core network-stack bug, not ours. **Don't downgrade the platform.**

History, kept for reference:

- Symptom: `assert failed: pbuf_free ... pbuf.c:755 (pbuf_free: p->ref > 0)`, then `Rebooting...`. In the weather firmware it fires right after `Loc:`. Up to 3 crashes in a row were seen before a boot got through.
- Decoded stack (same both times): `tcpip_thread → ethernet_input → ip4_input → tcp_input → tcp_process → tcp_receive → tcp_free_acked_segments → tcp_seg_free → pbuf_free`. lwIP's own thread frees a sent segment that was already freed.
- Isolation (`pebl_netstress`, arduino-esp32 3.3.2 / ESP-IDF 5.5.1): plain HTTP to ip-api 4/20 crashed, HTTPS to Open-Meteo 0/20, both in sequence 6/15. TLS is not the trigger; the plain `WiFiClient` path is.
- Ruled out: `HTTPClient` socket reuse. With `http.setReuse(false)` (`HTTP_NO_REUSE 1` in netstress) the second request still crashed.
- Confirmed: the core version. The old core crashed; 3.3.11 / IDF 5.5.5 ran 90 of 90 clean.
- Also seen once on the old core: after the 11th crash in a stress run, USB stopped answering auto-reset (the red LED was off; replugging fixed it). Needed the BOOT sequence.

### RESOLVED: double full refresh on every wake

`display.init(..., initial=true, ...)` made GxEPD2 blank the panel with its own full refresh before ours. Fixed with `initial=false` (every update here is full-window, which writes both buffers). Check: exactly one `_Update_Full` per render.

### RESOLVED: earlier flashing and boot failures

8 MB flash header on a 4 MB chip (boot loop), TLS handshake overflowing the 8 KB loop stack (`-D ARDUINO_LOOP_STACK_SIZE=16384`), and WiFiManager's portal blocking unattended wakes. See `flashing-and-recovery.md` and `wifi-and-provisioning.md`.

## How to get to it (user POV)

Nothing is open right now. On the old core the crash showed up only as a slow update: a few seconds of extra reboots before the screen changed.

## Driving it

- Regression check after any platform or network-code change: flash `pebl_netstress`, attach one logger for the whole run, and require 0 crashes in all three phases (30 rounds each). The old baseline was ~18% per HTTP request.
- Then a clean `pebl_weather` boot log with no `assert failed` / `Rebooting...`.

## Gotchas

- Decode backtraces only against the ELF whose SHA prefix matches the panic. Rebuilding with any change shifts the addresses.
- A crash that recovers still shows the right screen. Search the log for it; don't infer it from the panel.
