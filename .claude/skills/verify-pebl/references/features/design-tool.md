# Design tool

`tools/weather_design.py` is the single source for fonts, word art and layout previews. The previews are pixel-exact: they use the same glyph bitmaps and the same placement maths as the firmware.

## Sub-features

- font-convert: TTF → Adafruit GFX headers (`WeatherBig`, `WeatherHL`, `WeatherCond`, `WeatherDay`, `WeatherTmrw`) in `src/weather/weather_fonts.h`. A Python port of fontconvert (DPI 141, `yOffset = 1 - bitmap_top`, MSB bit packing)
- word-art: each word plus the sunglasses → `src/weather/weather_words.h` (Adafruit `drawBitmap` format)
- previews: `tools/preview.png` and `tools/preview_night.png` (numbers screen, day and night, 4× scale) and `tools/preview_words.png` (all eight word screens, day left, night right) and `tools/preview_tomorrow.png` (the night tomorrow screen)
- gap-check: prints worst-case gaps (Hi→big, Lo→condition, Hi→Lo, big→condition, weekday letter margin), then the tomorrow screen widths. It fails if the label or the widest range overflows the box
- ink alignment: `ink()` / `right_pen()` mirror Adafruit `getTextBounds`, so right-aligned text matches the device

## How to get to it (user POV)

```bash
cd /Users/willbraden/Documents/Development/pebl-esp32
~/.platformio/penv/bin/python tools/weather_design.py
open tools/preview.png tools/preview_night.png tools/preview_words.png tools/preview_tomorrow.png
```

Edit the CONFIG block (fonts, `H_*` sizes, baselines, `PREVIEW`, `WORST`, `WORDS`), rerun, look.

## Driving it

- Every layout change: run the tool, read the `worst gaps:` line (all ≥ 8 px, letter margin ≥ 2 px), view both PNGs, then copy the knobs into `renderWeather()` / `drawDayBar()` / `renderWord()`.
- Check the `PREVIEW` values with both 2- and 3-digit temps and a negative (the `WORST` set covers `108` and `-18`).
- After regenerating, `git diff --stat src/weather/weather_*.h` should only change what you meant to change. The converter is deterministic.

## Gotchas

- Font sizes are set by the pixel height of the digit "8", not by point size.
- The header is overwritten on every run. Never hand-edit `weather_fonts.h` or `weather_words.h`.
- Every generated font and word is JetBrains Mono Bold v2.304, kept in `tools/fonts/` with its OFL license, so the tool runs anywhere. The generated headers are committed, so building doesn't need the tool. Monospace digits are wide: the 3-digit Hi next to a 3-digit current temp is the binding case (exactly 8 px at Hi 37 / big 57).
- The status screens (`renderMessage()`) still use the built-in FreeSans 9 pt; the tool doesn't generate those.
- Preview and firmware share maths but not code. If you change placement in one, change the other. Both have comments pointing at each other.
