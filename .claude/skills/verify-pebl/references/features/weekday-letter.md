# Weekday letter

One letter on a row below the content, for the weekday of the reading. Its position is a week ruler: S at the left margin for Sunday, S at the right margin for Saturday, M T W T F evenly spaced between, so the day reads from where the letter sits as much as from the letter. It's on every weather screen (numbers, word, tomorrow). It exists because the board can't sense its battery: when the battery dies, the panel freezes on the last image, and a weekday that isn't today shows the screen is stale.

## Sub-features

- row: `WeatherDay` font, 11 px caps, baseline `DAY_BASE` = 113 (y 102–113): 9 px below the content block (y 9–93), 8 px above the bottom edge
- position: Sunday's ink starts at x = 8, Saturday's ends at x = 242, letter centres evenly spaced between (about 38 px apart)
- reading-date: weekday of Open-Meteo's `current.time`, not the device clock; the tomorrow screen uses the date the range is for

## How to get to it (user POV)

Always visible along the bottom edge.

## Driving it

- The weekday comes from `weekdayOf()` (0 = Sun … 6 = Sat). The serial `Temp ... @ MON ...` line shows the value used (`weekdayName()`).
- Layout: `drawDay()` in firmware and `day_x()` in the tool use the same maths. The tool asserts every day stays inside the 8 px margins and prints `content->day 9px, day->bottom 8px`.

## Gotchas

- A cached redraw (USER SW press) shows the weekday of the cached reading, not the current day. Normally that's the same, since the cache is under 30 minutes old.
- A weekday repeats after 7 days, so a device that's been dead exactly a week looks fresh. Accepted trade-off.
- Tuesday/Thursday and Sunday/Saturday share a letter; position tells them apart.
- `CachedWeather.day` / `tmrwDay` are `int8_t`, -1 when the reading had no date (nothing drawn).
