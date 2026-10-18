# Word screen

Mode 1. One large word (or a sunglasses icon) that sums up the weather, readable across a room.

## Sub-features

- precip-words: SNOW / RAIN when the chance of precipitation in the next 6 hours is ≥ 50%
- temp-words: by current temperature (°F), contiguous ranges
- shades-icon: sunglasses silhouette for 70–84°F
- fit-to-box: each word is its own bitmap, scaled to the largest size that fits the box
- weekday-letter: same bottom row as the numbers screen

Rules (`wordArtFor()`), first match wins:

| Condition | Shows |
|---|---|
| precip ≥ 50% in next 6 h and any snow in that window | SNOW |
| precip ≥ 50% in next 6 h | RAIN |
| temp < 32 | FREEZE |
| 32–54 | COLD |
| 55–69 | CHILLY |
| 70–84 | sunglasses |
| 85–94 | HOT |
| ≥ 95 | HELL |

Temperature is the current reading, rounded. With `USE_FAHRENHEIT 0` it's converted to °F for the rules.

## How to get to it (user POV)

Press USER SW from the numbers screen. The choice is remembered through sleep, RESET and power-off.

## Driving it

- Every word: `tools/preview_words.png` (all eight screens, rendered from the same bitmaps the firmware draws). That covers words you can't produce on demand (HELL, SNOW).
- On device: switch to mode 1, then check that the word on the panel matches the rules applied to the `Temp ... precip6h` line.
- Art lives in `src/weather/weather_words.h`, generated. Sizes as of 2026-10-18 (JetBrains Mono Bold): SNOW 233×75, RAIN 233×76, FREEZE 230×50, COLD 234×78, CHILLY 234×52, HOT 187×84, HELL 232×74, SHADES 190×76. Box is the content block, x 8–242, y 6–90.

## Gotchas

- Ranges were stretched to leave no gaps (the original spec left 44–54 and 66–69 empty). FREEZE was added later below 32.
- The art is pre-rendered. Changing a word or font means rerunning `tools/weather_design.py`; never hand-edit `weather_words.h`.
- The sunglasses are drawn in the tool (`render_shades()`), not traced from an image, so there's no copyright question.
- Words are different sizes on purpose (HOT is bigger than CHILLY). That matches the user's mockup.
