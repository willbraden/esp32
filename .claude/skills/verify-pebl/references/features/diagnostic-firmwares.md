# Diagnostic firmwares

Standalone probe builds, each with its own env and source folder (`build_src_filter = -<*> +<folder/>`; the shared `[env]` in `platformio.ini` supplies the board, flash size and partitions). Use them to answer hardware questions instead of guessing. Flashing one replaces the weather firmware until you flash `pebl_weather` back.

## Sub-features

| Env | Source | Answers |
|---|---|---|
| `pebl_battprobe` | `src/battprobe/` | Battery sense. The committed version (v5) is a fast-drain monitor that shows GPIO18 on the panel every ~10 s. The earlier ADC/pull/enable-line/I2C scans were never committed; their results are in `power-and-battery.md` |
| `pebl_btnprobe` | `src/btnprobe/` | Which pin a button is on. Samples every pin under pull-up **and** pull-down each loop, including display pins, and prints `Gn: float -> GND` style changes |
| `pebl_netstress` | `src/netstress/` | Network crash isolation. v4 (current) is the A/B test of the `pbuf_free` fix: one plain-HTTP request per boot then `ESP.restart()`, 30 boots per phase: old code (control), fixed, fixed HTTP then HTTPS, fixed ordering with keep-alive on. Tags each crash with the step it interrupted. Results are saved to NVS after every round, so they survive the USB link dying, RESET and power-off. Earlier versions are in git history |
| `pebl_otafail` | `src/otafail/` | OTA rollback test image: never confirms itself, reboots after 5 s. Ship it signed through the staging channel to prove rollback |

## How to get to it (user POV)

Not user-facing. The user presses buttons or unplugs USB when a probe asks for it.

## Driving it

```bash
~/.platformio/penv/bin/pio run -e pebl_btnprobe -t upload --upload-port $PORT
~/.platformio/penv/bin/python tools/serial_log.py --quiet --timeout 300
# ask the user to press USER SW a few times, then BOOT once
```

Expected btnprobe result: `G16: 3V3 -> GND` per USER SW press, `G0: float -> GND` for BOOT.

Read netstress results from flash (they're also printed over serial while USB works):

```bash
~/.platformio/penv/bin/python tools/boot_log.py --ns netstress     # board in download mode
```

v4 results (2026-10-13): `p0 ok=30 crash=24 restart:24 | p1 ok=30 crash=0 | p2 ok=30 crash=0 | p3 ok=30 crash=0`. The full history is in `known-issues.md`.

## Gotchas

- None of these deep-sleep, so the port stays up and `-t upload` auto-resets them. No BOOT sequence needed while a probe is running.
- Probes don't save state to NVS, except that WiFi credentials are untouched. Flashing `pebl_weather` back restores normal behavior, and the saved mode is kept.
- netstress keeps its results across RESET and power-off; only a new build starts a new run (it compares the build timestamp). To rerun the same build, erase the `netstress` namespace or rebuild.
- The USB link can die mid-run (`known-issues.md`). The run carries on; read the results from flash afterwards.
