# Weekday letter

One letter on a row below the content, for the weekday of the reading. Its position is a week ruler: S at the left margin for Sunday, S at the right margin for Saturday, M T W T F evenly spaced between, so the day reads from where the letter sits as much as from the letter. It's on every weather screen (numbers, word, tomorrow). It exists because the board can't sense its battery: when the battery dies, the panel freezes on the last image, and a weekday that isn't today shows the screen is stale.

## Sub-features

- glyphs: blocky letters from Figma (file `qoi55sJne29YRGgYxpUSd3`, node `1297:15813`, "Component 19"), 32×20, a 5×5 grid of cells (column widths 6/7/6/7/6, rows 4 px). Defined in `DAY_GLYPHS` in the tool, generated into `src/weather/weather_days.h`, drawn with `fillRect` per cell (no font)
- row: `DAY_TOP` = 96 (y 96–115): 6 px below the content block (y 6–90), 6 px above the bottom edge
- position: Sunday's left edge at x = 8, Saturday's right edge at x = 242, evenly spaced between (left edges 8, 41, 75, 109, 142, 176, 210)
- reading-date: weekday of Open-Meteo's `current.time`, not the device clock; the tomorrow screen uses the date the range is for

## How to get to it (user POV)

Always visible along the bottom edge.

## Driving it

- The weekday comes from `weekdayOf()` (0 = Sun … 6 = Sat). The serial `Temp ... @ MON ...` line shows the value used (`weekdayName()`).
- Layout: `drawDay()` in firmware and `day_x()` / `draw_day()` in the tool use the same maths. The tool asserts Sunday and Saturday land on the 8 px margins and prints `top 6px, content->day 6px, day->bottom 6px`.
- Changing a letter: edit `DAY_GLYPHS` in the tool and rerun it; don't edit `weather_days.h` by hand.

## Gotchas

- A cached redraw (USER SW press) shows the weekday of the cached reading, not the current day. Normally that's the same, since the cache is under 30 minutes old.
- A weekday repeats after 7 days, so a device that's been dead exactly a week looks fresh. Accepted trade-off.
- Tuesday/Thursday and Sunday/Saturday share a letter; position tells them apart.
- `CachedWeather.day` / `tmrwDay` are `int8_t`, -1 when the reading had no date (nothing drawn).
