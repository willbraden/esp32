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

### OPEN: panel FPC loses contact, so the glass goes white and stops responding (2026-10-18)

Twice on 2026-10-18 the panel went white and stayed white, while every refresh logged a normal BUSY (~2270–2290 ms, `_Update_Full`) on USB and on battery, across v1.3.1, v1.4.1 and v1.4.2. Reseating the FPC fixed it both times. Neither a chip reset nor a 10 s power-switch cycle did.

Decisive check: watch the panel during a refresh. A driven panel visibly flashes black and white for ~2 s. In this state **nothing moves**. The SSD1680 accepts commands and runs its waveform, but no drive voltage reaches the glass. So it isn't firmware: a bug drawing a white image would still flash. The panel went white rather than freezing on the old image, so one drive line probably failed partway through a refresh.

Both times it broke right after an OTA install from GitHub (#362→#363 for v1.4.1, #381→#382 for v1.4.2), as did the v1.1.1 blank below. USB flashes and normal wakes in between drew fine. Possibly the long Wi-Fi download warms the board enough to shift a marginal contact, or it's coincidence. Either way the fault is the connection, and the fix is mechanical: seat the FPC with the latch fully opened and closed, and hold it (tape or foam over the connector) or replace the connector or panel if it keeps happening.

Later on 2026-10-18 a reseat held for under an hour. After it, the panel froze on a 2:45 PM reading (76°) for ~4 hours while the board woke on schedule (#387–393), fetched fresh data, and logged normal refreshes. A live boot at 6:50 PM fetched 63.4° and night and drew the Tomorrow screen, but the panel went white instead. So the fault has two faces: **frozen** (the contact is fully open, so nothing reaches the glass and the old image stays) and **white** (partly open). Both look like "not updating" while the firmware does its job. This morning's stale 54° was the frozen face. Handling (plugging USB, pressing BOOT/RESET) flexes the board next to the connector and seems to trigger it.

Most likely a cracked trace in the panel's FPC tail near the connector, or a worn connector. Reseating moves the crack and helps only briefly. Inspect the FPC where it bends into the connector, and replace the panel (the FPC is part of it) if it keeps coming back.

How to recognise it: the panel is white or frozen on an old reading, the boot log shows wakes on schedule with `draw=...:~2280ms`, and the panel doesn't flash while the board refreshes. Don't debug firmware for this.

### WATCHING: blank panel after the first draw of an OTA update

`weather-v1.1.1`, 2026-10-13: the long press installed it (boot log #104 `ota=installing-1.1.1`), and its first boot (#105) fetched, drew (`path=...>drawn>ota>sleep`), checked (`ota=up-to-date`) and confirmed itself (otadata state `0x2` VALID). But the panel showed **pure white** instead of the weather. A fresh boot of the same image (watchdog reset, on USB) drew normally. The board was on battery during the update. Crash restarts earlier the same day were followed by normal draws, so a plain software restart isn't enough to cause it. What was different: a full refresh ("Updating firmware...") about 30 s before the restart, with the panel left powered, and battery power.

Hypotheses: (1) the SSD1680 is left in a state the next init doesn't recover from, because the panel isn't powered off before the install restart; (2) the boost converter can't reach drive voltage on battery under Wi-Fi load right after the download, washing the image out.

Not reproduced (2026-10-13) in four updates with the per-draw panel timing: three on the staging rig (`pebl_weather_staging` 1.3.0 -> 1.3.3), then the real `weather-v1.1.2` release installed the same way as the failing one. Every trial boot drew the weather on the panel:

| Update | Power | Download | Trial boot |
|---|---|---|---|
| 1.3.0 -> 1.3.1 | USB (Mac) | ~15 s, local HTTP | #114 `draw=numbers:2284ms usb=host`, weather on screen |
| 1.3.1 -> 1.3.2 | battery | ~15 s, local HTTP | #118 `draw=numbers:2284ms usb=none`, weather on screen |
| 1.3.2 -> 1.3.3 | battery | ~70 s, throttled to 24 KB/s like the GitHub download | #121 `draw=numbers:2284ms usb=none`, weather on screen |
| `weather-v1.1.2` from GitHub, long press | battery | real TLS download | #126 `draw=numbers:2284ms usb=none`, weather on screen, confirmed. An exact replay of the failing case |

Every refresh in those runs, including each "Updating firmware..." screen, held BUSY for 2284 ms. Hypothesis (1) as stated is also weakened by the code: GxEPD2's `init()` pulses the panel's RST line on every boot. The only thing the replay couldn't match is the battery's charge at the time (it had been on battery through a day of testing; this board has no battery sense, so it can't be logged). Treated as a one-off, most likely a low battery, until the boot log shows it again.

Since then every draw is in the boot log (`draw=<screen>:<ms> usb=host|none`, see `serial-diagnostics.md`). If it happens again, read the boot log before anything else: ~2300 ms on the blank boot means the panel ran a full refresh and the image or the drive voltage was wrong; near 0 means the panel never ran it; `TIMEOUT` means it hung.

### FIXED: OTA updates failed their trial boot (2026-10-13)

`weather-v1.1.0` (fonts only) downloaded and installed, then rolled back 3 times and was skipped for good (NVS `pebl-ota/bad_cnt=3`); it reached the device by USB instead. Causes, all fixed:

- **The `pbuf_free` crash** (above) hit right after `Loc:` on most boots, and the OTA code's own manifest fetch and download had the same use-after-free. Any crash on a trial boot rolls back.
- **The install restart counted as a power-on**, so a Wi-Fi miss opened the 180 s setup portal (photographed). Now the portal opens only when a person started the boot (power-on, RESET, USB reset) and never on a trial boot.
- **No retries.** A trial boot now tries Wi-Fi 3× and each fetch 2×.
- **Deep sleep never rolls back** (`CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP`): an unconfirmed image kept running wake after wake and only rolled back when something reset it, the crash in practice. Now it counts its failed boots and rolls itself back after 3 (`esp_ota_mark_app_invalid_rollback_and_reboot()`).
- **Back-to-back attempts.** The boot after a rollback is a fresh boot, and fresh boots check at once. Now that boot waits for the normal schedule, so the 3 attempts are spread out.

Proof on the staging rig (`pebl_weather_fault`, 1-minute wakes, check every 3rd wake, 2026-10-13):

| Image | Forced Wi-Fi failures on trial boots | Result |
|---|---|---|
| 1.2.1 | 1 | first attempt failed, retry connected, confirmed: the trial boot reached the server 26 s after the download and `try_ver` was cleared |
| 1.2.2 | all | each attempt: 3 trial boots with `fail=wifi trial`, no portal, then self-rollback (`next-boot=sw-restart`); the next boot logged `rolled-back-from=1.2.2(attempt-n/3)` and did not check; attempts at 13:41, 13:49, 13:57 (one scheduled check between each); then `ota=skipped-bad` |

Proven through a real release: `weather-v1.1.1` installed from GitHub on a long press, and its trial boot confirmed itself first time (2026-10-13). (Its first draw came out blank; see the open issue above.)

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
