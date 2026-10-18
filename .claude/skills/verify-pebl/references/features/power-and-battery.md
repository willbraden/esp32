# Power and battery

What the board does on battery, and the proven fact that firmware cannot read the battery level.

## Sub-features

- battery-run: LiPo keeps the board running with USB unplugged; the power switch cuts it
- no-battery-sense: there is no way to read voltage or percentage from firmware (proven)
- stale-cue: the weekday letter is the software stand-in for a low-battery warning
- divider-option: the hardware change that would add a real reading

## How to get to it (user POV)

Unplug USB with the switch on: the display keeps updating every 30 minutes until the cell is empty. Then the last image stays on the panel indefinitely (e-ink needs no power to hold it). The weekday letter on the bottom row stops matching today. That's the only sign.

## Driving it

Findings (2026-10-12, `pebl_battprobe` v3–v5):

- No divider on any ADC pin (GPIO1–7, 11, 15, 17, 18) with pulls disabled.
- No I2C device on any pair of externally pulled-up pins (only GPIO18 is pulled up, so no bus exists).
- No enable-gated divider: driving each free pin HIGH/LOW changed no ADC reading by more than 500 mV.
- GPIO18 stayed at full scale for ~8 h of forced drain (WiFi scanning nonstop) until power cut. The last frame was a normal sample, not a reboot. Power drops abruptly at the protection cutoff with no supply sag and no brownout warning to catch.
- Forced-drain runtime was ~8 h at roughly 150 mA. Real duty cycle (wake ~10 s per 30 min) should give days.

To add a real reading: 2 × 100 kΩ in series from battery + to GND, midpoint to **GPIO1** (ADC1 ch0, works with WiFi on). Read it once per wake before WiFi starts. Then a full-screen "CHARGE" below 10% becomes possible (4.2 V full, ~3.4 V empty).

## Gotchas

- Don't "estimate" battery from wake counts. It can't tell a full charge from a partial one. The user rejected it as a workaround.
- A low-voltage-reset trigger (draw CHARGE after a brownout reset) doesn't work here: the drain test showed no reset before cutoff.
- A low battery may have caused the one blank first draw after an OTA update (`known-issues.md`, WATCHING). Without a voltage reading it can't be confirmed. The boot log's `draw=` timing and `usb=none` are the closest evidence.
- `src/main.cpp` (legacy Slack client, kept only for its WiFi code) has battery code, but it reads the wrong pins for the S3 and there is no divider to read anyway.
