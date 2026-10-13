# Numbers screen

Mode 0. Today's high and low stacked on the left, the big current temperature on the right with a one-word condition under it.

## Sub-features

- hi-lo: Hi over Lo, left-aligned at `LEFT_X = BAR_W + 8 = 27`, 37 px digits (`WeatherHL`)
- current-temp: right-aligned at `RIGHT_X = 242`, 57 px digits (`WeatherBig`, JetBrains Mono Bold like every generated font), no degree sign
- condition: one word right-aligned under the temp, 17 px (`WeatherCond`): Clear, Cloudy, Fog, Rain, Snow, Storm
- weekday-bar: left strip, see `weekday-bar.md`

## How to get to it (user POV)

Default screen. If the word screen is showing, press USER SW once.

## Driving it

Layout knobs (`renderWeather()` and the CONFIG block in `tools/weather_design.py`):

| Knob | Value | Rule |
|---|---|---|
| `HI_BASE` | 56 | Hi top lines up with the big temp's top (y = 19) |
| `LO_BASE` | 103 | Lo baseline = condition baseline |
| `CUR_BASE` | 76 | top at 19, level with Hi |
| `COND_BASE` | 103 | 8 px below the big temp |

Both columns are 84 px tall, centered (y = 19…103). Sizes were solved so the worst case (3-digit Hi beside a 3-digit current temp, e.g. 108 / 102) still leaves exactly 8 px. The tool prints `worst gaps: Hi->big 8px ...` on every run; any value under 8 is a regression.

Proof for a layout change: tool output + `tools/preview.png` + a panel photo. For data: the `Temp ...` serial line matches what's drawn.

## Gotchas

- Right alignment uses ink bounds (`getTextBounds`), not advance width. The tool mirrors this with `ink()` / `right_pen()`. Measuring by advance width overstated the gaps by about 12 px.
- "Thunderstorm" was shortened to "Storm" because it would collide with a 3-digit Lo. Don't bring long labels back without rerunning the gap check.
- Temperatures are rounded with `lroundf`; negatives render with a minus and fit (checked with -18 in the worst-case set).
