# Night mode

Between sunset and sunrise, the numbers and word screens draw white on black: the day screen with every pixel inverted, weekday letter included. Status screens ("WiFi reconnecting...", "Updating firmware...", errors) always use the day theme.

By default the tomorrow screen (`tomorrow-screen.md`) replaces both screens at night, so the inverted numbers and word screens show when a short press peeks at them, or with `NIGHT_TOMORROW=0`.

## Sub-features

- trigger: Open-Meteo `current.is_day` for the reading's location. `0` means night. A missing field means day.
- theme-swap: `setTheme()` swaps the `INK` / `PAPER` colors that every drawing call uses, so a night screen is an exact inverse of the day screen
- cached: `CachedWeather.night` stores the theme, so a USER SW redraw from cache keeps the reading's theme
- flag: `NIGHT_INVERT` (default 1). Build with `-DNIGHT_INVERT=0` to always use the day theme.

## How to get to it (user POV)

Automatic. The screen flips on the first refresh after sunset or sunrise, so up to 30 minutes late. Open-Meteo's 15-minute slots can add up to 15 more.

## Driving it

- Serial: the `Temp ... @ DAY time day|night` line ends with the theme it parsed. Draws log as `Drew numbers-night` / `Drew word-night`. The boot log records `draw=numbers-night:~2300ms`.
- Pixels: `tools/preview_night.png` (numbers screen) and the right-hand column of `tools/preview_words.png` (every word). The tool makes them by inverting the day screens, which is exactly what the firmware does.
- Check the flag at a given place and time: `curl -s "https://api.open-meteo.com/v1/forecast?latitude=LAT&longitude=LON&current=is_day&daily=sunrise,sunset&timezone=auto&forecast_days=1"`.
- Prove it on the device after local sunset: one timer wake (or a long press) with the panel photographed, then one short press to check the cached word screen is dark as well.

## Gotchas

- Night uses the reading's location (IP geolocation), not the device clock. The device has no wall clock.
- A cached redraw keeps the theme from the fetch, which can be up to ~29 minutes old. The next timer wake corrects it.
- The flip is aligned to the refresh schedule, not to the exact sunset time. Waking right at sunset would need a real clock (NTP) to know how far away it is.
