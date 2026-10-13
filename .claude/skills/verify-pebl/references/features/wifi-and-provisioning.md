# WiFi and provisioning

How the device gets online, and what it does when it can't.

## Sub-features

- direct-reconnect: `WiFi.begin()` with credentials saved in NVS, 25 s timeout (`WIFI_TIMEOUT_MS`)
- provisioning-portal: WiFiManager AP `pebl-weather`, 180 s, only when a person started the boot: power-on, RESET or a USB reset (`personStartedBoot()`), and never on an OTA trial boot
- unattended-failure: on timer or button wakes, crash restarts and the restart after an OTA install, never open the portal; show "WiFi reconnecting..." and retry next cycle
- trial-retry: an OTA trial boot tries WiFi 3 times (10 s, then 20 s, apart) and the location and weather fetches twice, because giving up rolls the update back

## How to get to it (user POV)

First setup: power on with no saved network, join the `pebl-weather` WiFi from a phone, pick the home network. After that it's automatic. If the router is down, the panel shows "WiFi reconnecting..." with the wake number and retries in 30 minutes.

## Driving it

- Happy path: `WiFi OK: <ip>` within a few seconds of boot.
- Failure on a timer/button wake: turn the router off (or test away from it), wait for a wake, and expect `WiFi reconnect failed on wake — retry next cycle` plus the "WiFi reconnecting..." screen.
- Portal path: needs the credentials erased, which wipes the user's setup. Only with the user's explicit OK, and they have to re-provision afterward.

## Gotchas

- The portal blocks for up to 180 s. It must never run unattended; an earlier version froze for minutes that way, and until 2026-10-13 it also opened after an OTA install restart (counted as a power-on), which made a Wi-Fi miss roll the update back. Keep the `attended` guard: reset reason power-on / RESET / USB, and not a trial boot.
- Dozens of `STA.cpp ... Reason: 1 - UNSPECIFIED` warnings before `WiFi OK` are normal reconnect chatter in this core. Hide them with `--quiet`.
- Credentials live in the WiFi driver's NVS store, so a network saved by the original factory pebl firmware still works. Erasing flash (`esptool erase_flash`) removes them.
- Older multi-network and QR-code provisioning code is kept, unbuilt, in `src/wifi/` and `src/main.cpp`. See `legacy-wifi-tooling.md` before porting any of it.
