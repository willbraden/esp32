# Mode button

USER SW switches between the numbers screen (mode 0) and the word screen (mode 1).

## Sub-features

- wake-on-press: ext0 wake on GPIO16 LOW from deep sleep
- toggle: mode = (mode + 1) % `MODE_COUNT`, saved before drawing
- persistence: NVS namespace `pebl-weather`, key `mode` (uchar); survives sleep, RESET and power-off
- fast-redraw: uses the RTC cache, no WiFi (see `wake-and-sleep-cycle.md`)
- release-wait: sleep is armed only after the button is released (≤ 5 s)
- cs-protection: GPIO16 drive strength set to `GPIO_DRIVE_CAP_0` while it's acting as CS

## How to get to it (user POV)

Press and release USER SW once. The panel refreshes to the other screen in a few seconds. Presses during a refresh are ignored (the pin is busy as CS); press again after it settles.

## Driving it

1. Start from a known mode (check the `Mode M` line on a cold boot).
2. With **no logger attached**, press USER SW. The user confirms the screen flipped, ideally with a photo.
3. Press again; it flips back.
4. Persistence: switch to mode 1, flip the power switch off and on with USB unplugged, and confirm it boots into the word screen.

Verified 2026-10-12: one press → CHILLY, second press → numbers.

## Gotchas

- USER SW is **not** on a free GPIO. It shares GPIO16 with display CS, which is why three probe passes over the "free" pins missed it.
- With a serial logger running, each button wake gets reset by the port reopen and logs as `power-on/reset`. The toggle has already happened by then, so the screen is still right but the log lies.
- BOOT (GPIO0) could serve as a second button after boot, but holding it through a reset enters download mode. It's not used.
- Adding a third mode: increase `MODE_COUNT` and handle it in `render()`. Old saved values ≥ `MODE_COUNT` fall back to numbers.
