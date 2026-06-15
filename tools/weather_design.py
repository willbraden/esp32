#!/usr/bin/env python3
"""
weather_design.py — edit the pebl weather display's fonts & preview the layout.

WHAT THIS DOES
  1. Converts the TTF fonts into the Adafruit-GFX header the firmware uses
     (writes src/weather/dimitri_fonts.h).
  2. Renders a 1-bit preview PNG of the layout (tools/preview.png) — exactly how
     the e-ink panel will show it — so you can iterate WITHOUT flashing.

It does NOT touch the device. After you like the preview:
    ~/.platformio/penv/bin/pio run -e pebl_weather          # build
    ...then flash (hold BOOT, tap RESET, keep holding).

RUN IT:
    cd <repo root>
    ~/.platformio/penv/bin/python tools/weather_design.py

The pio python already has freetype-py + pillow installed. Edit the CONFIG
block below, run, open tools/preview.png. When happy, copy the LAYOUT numbers
into renderWeather() in src/weather/weather_main.cpp (the "LAYOUT KNOBS" block)
so the device matches the preview, then build + flash.
"""

import freetype, io
from PIL import Image, ImageDraw

# ============================ CONFIG — EDIT ME ============================
# Font files (any .ttf). Swap these to try a totally different typeface.
FONT_BIG  = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"  # big current temp
FONT_HL   = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"  # small Hi / Lo
FONT_COND = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"  # condition word

# Glyph height in pixels for each element (bigger = bigger text).
H_BIG  = 76   # current temp
H_HL   = 28   # Hi / Lo
H_COND = 21   # condition word

# Layout positions (must match the LAYOUT KNOBS in weather_main.cpp).
# Screen is 250 wide x 122 tall. y values are text BASELINES.
# NOTE: baselines depend on the font's below-baseline extent. Plain sans (Arial)
# has ~none, so the temp sits at CUR_BASE=84. A beveled/3-D font that overhangs
# below the baseline needs a smaller CUR_BASE to leave a gap above the condition.
LEFT_X    = 8        # Hi/Lo left edge
RIGHT_X   = 250 - 8  # right edge that the temp + condition align to
HI_BASE   = 36       # Hi baseline
LO_BASE   = 84       # Lo baseline
CUR_BASE  = 84       # current-temp baseline
COND_BASE = 114      # condition baseline

# What to show in the preview (change to test different values / longest words).
PREVIEW = dict(hi="102", lo="89", cur="98", cond="Thunderstorm")
# ========================================================================

DPI = 141                       # matches Adafruit fontconvert
FIRST, LAST = 0x20, 0x7E        # printable ASCII
STRUCTS = dict(big="WeatherBig", hl="WeatherHL", cond="WeatherCond")
HEADER  = "src/weather/weather_fonts.h"
PREVIEW_PNG = "tools/preview.png"


def _pt_for_height(face, px):
    """Find the point size whose '8' renders ~px pixels tall."""
    best = (8, 0)
    for pt in range(6, 200):
        face.set_char_size(pt * 64, 0, DPI, 0)
        face.load_char('8', freetype.FT_LOAD_TARGET_MONO | freetype.FT_LOAD_RENDER)
        h = face.glyph.bitmap.rows
        if abs(h - px) < abs(best[1] - px):
            best = (pt, h)
        if h >= px:
            break
    return best[0]


def convert(path, struct, target_px):
    face = freetype.Face(path)
    face.set_char_size(_pt_for_height(face, target_px) * 64, 0, DPI, 0)
    glyphs, bitmaps, off = [], bytearray(), 0
    for cp in range(FIRST, LAST + 1):
        face.load_char(chr(cp), freetype.FT_LOAD_TARGET_MONO | freetype.FT_LOAD_RENDER)
        g = face.glyph; b = g.bitmap; w, h, pitch, buf = b.width, b.rows, b.pitch, b.buffer
        bits = [(buf[y * pitch + (x >> 3)] >> (7 - (x & 7))) & 1
                for y in range(h) for x in range(w)]
        while len(bits) % 8:
            bits.append(0)
        gb = bytearray()
        for i in range(0, len(bits), 8):
            v = 0
            for k in range(8):
                v = (v << 1) | bits[i + k]
            gb.append(v)
        glyphs.append(dict(off=off, w=w, h=h, xadv=g.advance.x >> 6,
                           xoff=g.bitmap_left, yoff=1 - g.bitmap_top, bits=bits))
        bitmaps += gb; off += len(gb)
    return dict(struct=struct, glyphs=glyphs, bitmaps=bitmaps, yadv=face.size.height >> 6)


def emit(f):
    s = f['struct']; o = [f"const uint8_t {s}Bitmaps[] PROGMEM = {{",
                          "  " + ", ".join(f"0x{b:02X}" for b in f['bitmaps']), "};",
                          f"const GFXglyph {s}Glyphs[] PROGMEM = {{"]
    for g in f['glyphs']:
        o.append(f"  {{ {g['off']}, {g['w']}, {g['h']}, {g['xadv']}, {g['xoff']}, {g['yoff']} }},")
    o.append("};")
    o.append(f"const GFXfont {s} PROGMEM = {{ (uint8_t*){s}Bitmaps, "
             f"(GFXglyph*){s}Glyphs, 0x{FIRST:02X}, 0x{LAST:02X}, {f['yadv']} }};")
    return "\n".join(o)


def text_w(f, s):
    return sum(f['glyphs'][ord(c) - FIRST]['xadv'] for c in s)


def blit(draw, f, s, penx, baseline):
    for c in s:
        g = f['glyphs'][ord(c) - FIRST]
        for y in range(g['h']):
            for x in range(g['w']):
                if g['bits'][y * g['w'] + x]:
                    draw.point((penx + g['xoff'] + x, baseline + g['yoff'] + y), fill=0)
        penx += g['xadv']


def main():
    big  = convert(FONT_BIG,  STRUCTS['big'],  H_BIG)
    hl   = convert(FONT_HL,   STRUCTS['hl'],   H_HL)
    cond = convert(FONT_COND, STRUCTS['cond'], H_COND)

    with open(HEADER, "w") as fh:
        fh.write("#pragma once\n#include <Adafruit_GFX.h>\n\n"
                 + emit(big) + "\n\n" + emit(hl) + "\n\n" + emit(cond) + "\n")
    print(f"wrote {HEADER}  (BIG {len(big['bitmaps'])}B, HL {len(hl['bitmaps'])}B, "
          f"COND {len(cond['bitmaps'])}B)")

    img = Image.new("L", (250, 122), 236); d = ImageDraw.Draw(img)   # e-ink gray
    blit(d, hl,   PREVIEW['hi'], LEFT_X, HI_BASE)
    blit(d, hl,   PREVIEW['lo'], LEFT_X, LO_BASE)
    blit(d, big,  PREVIEW['cur'],  RIGHT_X - text_w(big,  PREVIEW['cur']),  CUR_BASE)
    blit(d, cond, PREVIEW['cond'], RIGHT_X - text_w(cond, PREVIEW['cond']), COND_BASE)
    img.point(lambda x: 0 if x < 128 else 236).resize((1000, 488), Image.NEAREST).save(PREVIEW_PNG)
    print(f"wrote {PREVIEW_PNG}  (open it to see the layout)")


if __name__ == "__main__":
    main()
