# Weekday bar

A full-height black strip on the left edge with the weekday of the reading in white letters stacked vertically (M / O / N). It's on both screens. It exists because the board can't sense its battery: when the battery dies, the panel freezes on the last image, and a weekday that isn't today shows the screen is stale.

## Sub-features

- inverted-strip: `BAR_W` = 19 px black bar, x 0–18
- stacked-letters: `WeatherDay` font, 11 px caps, 8 px between letters, each letter centered in the bar, the stack centered vertically
- reading-date: weekday of Open-Meteo's `current.time`, not the device clock

## How to get to it (user POV)

Always visible on the left edge.

## Driving it

- Letters come from `weekdayAbbr()` (SUN…SAT). The serial `Temp ... @ MON ...` line shows the value used.
- Layout: `drawDayBar()` in firmware and `day_layout()` in the tool use the same maths. The tool prints `day letter margin 3.0px` (the widest letter, W, leaves 3 px on each side).

## Gotchas

- A cached redraw (USER SW press) shows the weekday of the cached reading, not the current day. Normally that's the same, since the cache is under 30 minutes old.
- A weekday repeats after 7 days, so a device that's been dead exactly a week looks fresh. Accepted trade-off.
- `CachedWeather.day` is `char[4]`, so the weekday must stay 3 letters.
