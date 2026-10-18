# Tomorrow screen

Between sunset and sunrise, the numbers and word screens give way to "Tomorrow" over the next day's low-high (`46-63`), drawn white on black. The weekday letter shows the day the range is for. When the sun comes up, the next refresh brings back the selected screen.

## Sub-features

- trigger: the same `is_day == 0` that picks the night theme (`night-mode.md`)
- which day: Open-Meteo's daily day 1 when the reading is after noon, day 0 before noon. So at 9 PM Monday and at 3 AM Tuesday it shows Tuesday: the next day you wake up to
- range: `lo-hi`, rounded. With a negative low it is `lo/hi` (`-18/-5`), so it never reads `-5--2`
- layout: both lines centred in the content block (x 8–242, y 6–90), the pair centred vertically with a 10 px gap. "Tomorrow" is small (`WeatherCond`, 17 px). The range uses the big current-temp font (`WeatherBig`, 57 px, spanning y=6..90 like the numbers screen) when it fits the 234 px box, which every 2-digit range does. A 3-digit temp (`98-102`, `108-112`) or a 3-character low (`-18/-5`) falls back to `WeatherHL` (37 px)
- night-peek: a short press swaps to the selected screen (numbers or word, in the night theme) and back. It does not change the saved mode. The next timer wake or long press goes back to tomorrow
- cached: `CachedWeather.tmrwHi/tmrwLo/tmrwDay` keep it for button redraws
- flag: `NIGHT_TOMORROW` (default 1). Build with `-DNIGHT_TOMORROW=0` for the old night behaviour (the selected screen, inverted)

## How to get to it (user POV)

Automatic after sunset, on the first refresh (up to ~30 minutes late, like the night theme). Press USER SW to see current conditions; press again, or wait for the next refresh, to get tomorrow back.

## Driving it

- Serial: `Tomorrow TUE  Lo 46.0  Hi 63.0` after the `Temp ...` line. The draw logs as `Drew tomorrow-night`, and the boot log records `draw=tomorrow-night:~2300ms`. A peek prints `Mode M (night peek)`.
- Pixels: `tools/preview_tomorrow.png`.
- Check the numbers: `curl -s "https://api.open-meteo.com/v1/forecast?latitude=LAT&longitude=LON&current=is_day&daily=temperature_2m_max,temperature_2m_min&temperature_unit=fahrenheit&timezone=auto&forecast_days=2"`.
- On device after sunset: one timer wake (or long press) and a photo, then a short press (selected screen, dark), then another (tomorrow again).

## Gotchas

- Noon is the split, not midnight, so the small hours still say "Tomorrow" for the coming day. The reading's own clock decides it, not the device's.
- At night a short press no longer changes the saved mode. Switch modes during the day.
- A cached peek can be ~29 minutes old, like any cached redraw.
