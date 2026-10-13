# Known issues

Open bugs with their evidence, and resolved ones that could come back.

## Sub-features

### FIXED: lwIP `pbuf_free: p->ref > 0` crash (2026-10-13)

**Root cause: a use-after-free in our own `httpGet()`.** It declared `HTTPClient http` before a stack `WiFiClient c`, so C++ destroyed `c` first. ip-api answers HTTP/1.1 keep-alive, so `http.end()` kept the connection "for reuse" and left `http._client` pointing at `c`. Then `~HTTPClient()` called `_client->stop()` on the destroyed client, which wrote `sockfd = -1` into freed heap memory and released its two `shared_ptr` control blocks a second time. That corrupts the heap allocator, and lwIP allocates its packet buffers from the same heap, so lwIP's thread hit a doubly-owned `pbuf` on the next network activity. The HTTPS path never crashed because its client is `static`. `src/weather/ota.cpp` had the same declaration order in both the manifest fetch and the firmware download.

Fix: the `HTTPClient` now lives inside `fetch(NetworkClient&, ...)`, so it is always destroyed before the client it points at; `ota.cpp` declares its clients first. Both also call `setReuse(false)` (every request is one-shot). **Rule: a client passed to `HTTPClient::begin()` must outlive the `HTTPClient`.**

Evidence (`pebl_netstress`, one request per boot, 30 boots per phase, 2026-10-13):

| Run | Phase | Crashes |
|---|---|---|
| v3.1 | old code, plain HTTP | 28/30, all in the restart after the request |
| v3.1 | HTTPS only | 0/30 |
| v3.1 | old code, plain HTTP, 3 s after Wi-Fi | 23/30 (not a timing race) |
| v3.1 | old code, HTTP then HTTPS (the weather sequence) | 18/30, all at the next HTTPS request |
| v4 | old code (control) | 24/30 |
| v4 | fixed | **0/30** |
| v4 | fixed, HTTP then HTTPS | **0/30** |
| v4 | fixed ordering only, keep-alive on | **0/30** (ordering alone is the fix) |

Before the fix the weather firmware crashed on 4 of 6 boots (boot log, 2026-10-13), always right after `Loc:`, and the crash rolled OTA updates back.

Earlier wrong turns, kept so nobody repeats them: the 55.03.311 platform upgrade, TLS, PSRAM, source-port reuse and "HTTPClient reuse" (an old-core test with `setReuse(false)` still crashed; that core had a second steady-state crash, which confounded it) were all chased first. The decoded stack (`tcpip_thread → tcp_input → tcp_receive → tcp_free_acked_segments → pbuf_free`) only shows where the corruption was noticed, not who caused it. Espressif's maintainers say the same of this assert: look for memory corruption elsewhere.

### OPEN: the USB serial/JTAG link dies while the firmware keeps running

This is what earlier notes called "the board wedges after a software reboot". **The chip does not hang.** On 2026-10-13 a "wedged" board kept answering pings in netstress's connect/restart rhythm, and later turned out to have finished all 120 rounds, while USB gave nothing: no serial, `esptool` "No serial data received" even with a USB reset, and OpenOCD `libusb_get_string_descriptor_ascii() failed`. macOS still lists the device because it cached the descriptors. The ESP32-S3's USB serial/JTAG peripheral deliberately survives CPU resets (so a debugger stays attached), so once stuck it stays stuck through every `ESP.restart()` and panic. Only RESET (or a USB replug) clears it.

Seen after crash reboots and once mid-run with no crash (netstress HTTPS-only round 10). The "serial output stops after `_Update_Full`" blackouts look like the same thing: boot log #7 proved the firmware finished normally while serial was silent. Cause unknown. In the field (battery, no USB) it doesn't matter; it only costs development time. Work with it: read results from flash (`tools/boot_log.py`) instead of trusting serial, and tap RESET to get the port back.

### OPEN: OTA updates fail their trial boot

`weather-v1.1.0` (fonts only) downloaded and installed, then rolled back 3 times and was skipped for good (2026-10-13; NVS `pebl-ota/bad_cnt=3`, read with esptool). It reached the device by USB instead and runs fine. One failed trial was photographed: Wi-Fi didn't connect after the install restart, the firmware treated the restart as a power-on and opened the 180 s setup portal, then slept unconfirmed, which counts as a rollback. The other two left no evidence (the boot log didn't exist yet), but the `pbuf_free` crash above hit right after `Loc:` on most boots, and the OTA code's own manifest fetch and download had the same use-after-free. Both are fixed. Three design flaws remain, and they still turn any trial-boot failure (slow Wi-Fi after a restart, a router blip) into a failed update:

- The trial boot treats `sw-restart` like a power-on: it opens the setup portal instead of behaving as an unattended wake.
- A rollback boot is a cold boot, and cold boots check for updates at once, so all 3 attempts burn within minutes.
- Any failure on the trial boot (network included) rolls the image back, not just failures the image caused.

Until these are fixed (step 3 of the 2026-10-13 OTA plan), treat OTA as unreliable and prove each release on the device.

### RESOLVED: double full refresh on every wake

`display.init(..., initial=true, ...)` made GxEPD2 blank the panel with its own full refresh before ours. Fixed with `initial=false` (every update here is full-window, which writes both buffers). Check: exactly one `_Update_Full` per render.

### RESOLVED: earlier flashing and boot failures

8 MB flash header on a 4 MB chip (boot loop), TLS handshake overflowing the 8 KB loop stack (`-D ARDUINO_LOOP_STACK_SIZE=16384`), and WiFiManager's portal blocking unattended wakes. See `flashing-and-recovery.md` and `wifi-and-provisioning.md`.

## How to get to it (user POV)

Before the fix, the crash showed up as a slower update (extra reboots before the screen changed) and as OTA updates that rolled back. The USB link failure isn't visible to the user at all; the panel keeps updating.

## Driving it

- Regression check after any platform or network-code change: flash `pebl_netstress` (v4), let it run all 120 boots (~10 min; it doesn't need USB), then read `tools/boot_log.py --ns netstress` in download mode. Phase 0 (the old code, kept as a control) should still crash on most boots; phases 1-3 must show 0.
- Then the weather firmware's boot log over many wakes: no record with `next-boot=PANIC`.
- Any new `HTTPClient` code: the client object must be declared before the `HTTPClient` (or the `HTTPClient` confined to a helper like `fetch()`).

## Gotchas

- Decode backtraces only against the ELF whose SHA prefix matches the panic. Rebuilding with any change shifts the addresses.
- A crash that recovers still shows the right screen. Search the log for it; don't infer it from the panel.
