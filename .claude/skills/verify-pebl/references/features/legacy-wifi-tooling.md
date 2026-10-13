# Legacy WiFi tooling

The repo started as the pebl Slack/Discord reactions client for LilyGo T5 boards. Most of that client was removed. Its WiFi tooling was kept on purpose as reference for future work, such as multi-network WiFi for the weather firmware. **No env builds it**, and it can't build as it stands. The rest of the old client is in git history at `4a4c9a5`.

| Kept | What it is |
|---|---|
| `src/wifi/WiFiCredentialManager.{h,cpp}` | Up to 5 saved networks (`MAX_NETWORKS`) in NVS namespace `wifi_creds`, with least-recently-used eviction. "Pinned" seed networks come from `config.json` and are never evicted |
| `src/config/ConfigManager.{h,cpp}` | Loads `data/config.json` from LittleFS. The credential manager reads its `wifi.seed_networks` |
| `data/config.json.example` | The config format, including `seed_networks`. The real `data/config.json` is gitignored and holds secrets |
| `src/main.cpp` | The old app. The WiFi parts: `connectWiFi()` (`:3880`, hands every saved network to `WiFiMulti`, which joins the strongest one in range; 3 attempts, then provisioning), `startProvisioning()` (`:4198`, WiFiManager portal `pebl-setup`, 600 s timeout, saves the new network into the credential manager), `showProvisioningMode()` (`:1422`, `WIFI:` QR code on the panel so a phone joins the AP), and `handleButtonHold()` (`:4590`, 15 s hold opens the portal) |

## Sub-features

- multi-network: join the strongest saved network in range; record which one connected (`updateLastUsed`) so eviction drops the stalest
- seed networks: networks pinned from a config file, merged with NVS on boot
- provisioning: captive portal plus an on-panel QR code to join it
- button-triggered portal: hold to re-provision without erasing flash

## How to get to it (user POV)

Not user-facing. Nothing on the device runs this code. The weather firmware has its own, simpler WiFi path (see `wifi-and-provisioning.md`).

## Driving it

Can't be driven as is. To reuse a piece, port it into `src/weather/`:

- Replace `logMessage(...)` (defined in `main.cpp`) and the `ConfigManager` seed-network lookup with weather-firmware equivalents.
- Board differences: the old code's BOOT button is GPIO39 (`main.cpp:3176`). On this board BOOT is GPIO0 and USER SW is GPIO16, shared with display CS (`hardware-and-pins.md`). The old QR layout assumes a 212 × 104 panel, not 250 × 122.
- Prove it like any weather feature: the serial log plus a photo of the panel (`SKILL.md` proof bar).

## Gotchas

- `main.cpp` still includes `security/`, `resilience/` and `ota/` headers that were removed. Read it for the WiFi flow; don't try to build it.
- The weather firmware stores its network in the WiFi driver's own NVS store (`WiFi.begin()` with no arguments). The legacy manager uses a separate `wifi_creds` namespace. A port has to choose one, or migrate, or devices lose their network on the update.
- `getBatteryStatus()` in `main.cpp` reads classic-ESP32 ADC pins. It's wrong on the S3, and this board has no battery divider anyway (`power-and-battery.md`).
- No env compiles `src/main.cpp`, `src/config/` or `src/wifi/`, because every env sets its own `build_src_filter`. A new env must do the same, or it will pick these up along with a second `setup()`.
