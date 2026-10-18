#!/usr/bin/env python3
"""
weather_design.py — edit the pebl weather display's fonts & preview the layout.

WHAT THIS DOES
  1. Converts the TTF fonts into the Adafruit-GFX header the firmware uses
     (writes src/weather/weather_fonts.h).
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

import freetype, io, math
from PIL import Image, ImageDraw

# ============================ CONFIG — EDIT ME ============================
# Font files (any .ttf). Swap these to try a totally different typeface.
# JetBrains Mono Bold v2.304 (SIL OFL 1.1, tools/fonts/JetBrainsMono-OFL.txt),
# kept in the repo so the output doesn't depend on what's installed.
FONT_MAIN = "tools/fonts/JetBrainsMono-Bold.ttf"
FONT_BIG  = FONT_MAIN  # big current temp
FONT_HL   = FONT_MAIN  # Hi / Lo
FONT_COND = FONT_MAIN  # condition word

# Glyph height in pixels for each element (bigger = bigger text).
# Sized so the WORST case below keeps >= 8px between neighbours (a 3-digit Hi
# next to a 3-digit current temp; JetBrains Mono's digits are wider than Arial's).
H_BIG  = 57   # current temp
H_HL   = 37   # Hi / Lo
H_COND = 17   # condition word

# Screen grid (must match the constants above drawDay() in weather_main.cpp).
# Screen is 250 wide x 122 tall. y values are text BASELINES.
# Content fills x=8..242, y=6..90; the weekday letter has its own row below
# (y=96..115), sliding along it like a week ruler: S at the left margin,
# S at the right margin, M T W T F evenly spaced between. 6px above the
# content, between it and the letter, and below the letter.
MARGIN      = 8
CONTENT_TOP = 6
CONTENT_BOT = 90
DAY_TOP     = 96

# Weekday letters: the blocky glyphs from Figma ("Component 19", node 1297:15813,
# file qoi55sJne29YRGgYxpUSd3). Each is 32x20, a 5x5 grid of 6.4 x 4 px cells,
# rounded here to column widths 6,7,6,7,6. Drawn as filled cells, not a font.
DAY_COL_X = [0, 6, 13, 19, 26, 32]     # column edges; glyph width = DAY_COL_X[-1]
DAY_ROW_H = 4                          # glyph height = 5 * DAY_ROW_H = 20
DAY_GLYPHS = {
    "S": ["#####", "#....", "#####", "....#", "#####"],
    "M": ["#####", "#.#.#", "#.#.#", "#.#.#", "#.#.#"],
    "T": ["#####", "..#..", "..#..", "..#..", "..#.."],
    "W": ["#.#.#", "#.#.#", "#.#.#", "#.#.#", "#####"],
    "F": ["#####", "#....", "#####", "#....", "#...."],
}
DAYS_HEADER = "src/weather/weather_days.h"

# Numbers screen (must match the LAYOUT KNOBS in renderWeather()).
# Both columns are 84px tall and fill the content block.
LEFT_X    = MARGIN        # Hi/Lo left edge
RIGHT_X   = 250 - MARGIN  # right edge that the temp + condition align to
HI_BASE   = CONTENT_TOP + H_HL    # Hi baseline (top aligned with the big temp's top)
LO_BASE   = CONTENT_BOT   # Lo baseline   (aligned with the condition baseline)
CUR_BASE  = CONTENT_TOP + H_BIG   # current-temp baseline (top level with Hi)
COND_BASE = CONTENT_BOT   # condition baseline

# ---- tomorrow screen (replaces both screens between sunset and sunrise) ----
# A small "Tomorrow" (condition font) over tomorrow's low-high range in the big
# current-temp font, or the Hi/Lo font when that is too wide (3-digit temps).
# Both centred in the content block, the pair centred vertically.
# Must match renderTomorrow().
TMRW_GAP = 10           # label bottom -> range top. With the big font: y=6..90
PREVIEW_TMRW_PNG = "tools/preview_tomorrow.png"

# ---- word mode (USER SW toggles between the numbers screen and this) ----
# Each word is rendered as its own bitmap, scaled to the largest size that fits
# the content block (so short words like HOT come out bigger than CHILLY).
FONT_WORD  = FONT_MAIN
WORD_BOX   = (MARGIN, CONTENT_TOP, 250 - MARGIN, CONTENT_BOT)   # x0, y0, x1, y1
WORDS      = ["SNOW", "RAIN", "FREEZE", "COLD", "CHILLY", "HOT", "HELL"]
SHADES_W   = 190      # sunglasses icon width (height follows its 2.5:1 shape)
WORDS_HEADER = "src/weather/weather_words.h"
WORDS_PNG    = "tools/preview_words.png"

# What to show in the preview (change to test different values / longest words).
PREVIEW = dict(hi="103", lo="74", cur="102", cond="Clear", day=1,      # 0=Sun..6=Sat
               tmrw=["46-63", "98-102"], tmrw_day=2)
# Worst cases checked for collisions on every run.
WORST = dict(nums=["108", "-18"], conds=["Cloudy", "Storm", "Clear", "Rain", "Snow", "Fog"],
             ranges=["108-112", "98-102", "-18/-5", "-5/12", "99-99"])
# ========================================================================

DPI = 141                       # matches Adafruit fontconvert
FIRST, LAST = 0x20, 0x7E        # printable ASCII
STRUCTS = dict(big="WeatherBig", hl="WeatherHL", cond="WeatherCond")
HEADER  = "src/weather/weather_fonts.h"
PREVIEW_PNG = "tools/preview.png"
PREVIEW_NIGHT_PNG = "tools/preview_night.png"


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


def ink(f, s):
    """(x1, w) ink extent of s drawn at pen x=0 — same as Adafruit getTextBounds."""
    pen, lo, hi = 0, None, None
    for c in s:
        g = f['glyphs'][ord(c) - FIRST]
        if g['w']:
            lo = pen + g['xoff'] if lo is None else min(lo, pen + g['xoff'])
            hi = pen + g['xoff'] + g['w'] if hi is None else max(hi, pen + g['xoff'] + g['w'])
        pen += g['xadv']
    return (lo or 0), ((hi - lo) if lo is not None else 0)


def right_pen(f, s):
    """Pen x that puts the ink's right edge on RIGHT_X (mirrors the firmware)."""
    x1, w = ink(f, s)
    return RIGHT_X - w - x1


def blit(draw, f, s, penx, baseline, fill=0):
    for c in s:
        g = f['glyphs'][ord(c) - FIRST]
        for y in range(g['h']):
            for x in range(g['w']):
                if g['bits'][y * g['w'] + x]:
                    draw.point((penx + g['xoff'] + x, baseline + g['yoff'] + y), fill=fill)
        penx += g['xadv']


DAY_LETTERS = "SMTWTFS"


DAY_W, DAY_H = DAY_COL_X[-1], 5 * DAY_ROW_H


def day_x(wd):
    """Left edge of weekday wd's letter (0=Sun..6=Sat) - mirrors drawDay() in weather_main.cpp:
    Sunday at the left margin, Saturday ending at the right margin, evenly spaced."""
    return MARGIN + (250 - 2 * MARGIN - DAY_W) * wd // 6


def draw_day(draw, wd, fill=0):
    x0 = day_x(wd)
    for r, row in enumerate(DAY_GLYPHS[DAY_LETTERS[wd]]):
        for c, on in enumerate(row):
            if on == "#":
                draw.rectangle((x0 + DAY_COL_X[c], DAY_TOP + r * DAY_ROW_H,
                                x0 + DAY_COL_X[c + 1] - 1, DAY_TOP + (r + 1) * DAY_ROW_H - 1), fill=fill)


def build_days():
    """Write DAYS_HEADER: one 5-bit mask per glyph row (bit 4 = left column)."""
    lines = ["#pragma once", "#include <Arduino.h>", "",
             "// Generated by tools/weather_design.py - do not edit by hand.",
             "// Weekday letters from Figma: 5x5 grids of cells, drawn with fillRect.", "",
             f"static const int DAY_W = {DAY_W}, DAY_H = {DAY_H}, DAY_ROW_H = {DAY_ROW_H};",
             "static const uint8_t DAY_COL_X[6] = {" + ", ".join(map(str, DAY_COL_X)) + "};",
             "// Rows of each weekday's letter, Sunday first.",
             "static const uint8_t DAY_ROWS[7][5] = {"]
    for wd, l in enumerate(DAY_LETTERS):
        masks = [int(row.replace("#", "1").replace(".", "0"), 2) for row in DAY_GLYPHS[l]]
        lines.append("    {" + ", ".join(f"0x{m:02X}" for m in masks) + f"}},   // {l}")
    lines += ["};", ""]
    with open(DAYS_HEADER, "w") as fh:
        fh.write("\n".join(lines))
    print(f"wrote {DAYS_HEADER}")


def centre_pen(f, s):
    """Pen x that centres the ink in the content block (mirrors renderTomorrow())."""
    x0, _, x1, _ = WORD_BOX
    ix, w = ink(f, s)
    return x0 + (x1 - x0 - w) // 2 - ix


def tmrw_layout(big, hl, rng):
    """(range font, its height, label baseline, range baseline) - mirrors renderTomorrow()."""
    x0, _, x1, _ = WORD_BOX
    f, h = (big, H_BIG) if ink(big, rng)[1] <= x1 - x0 else (hl, H_HL)
    top = CONTENT_TOP + (CONTENT_BOT - CONTENT_TOP - (H_COND + TMRW_GAP + h)) // 2
    return f, h, top + H_COND, top + H_COND + TMRW_GAP + h


def check(big, hl, cond):
    """Print the tightest gaps for the worst-case strings."""
    hlRight = max(LEFT_X + sum(ink(hl, n)) for n in WORST['nums'])      # ink right edge
    bigLeft = min(right_pen(big, n) + ink(big, n)[0] for n in WORST['nums'])
    cndLeft = min(right_pen(cond, c) + ink(cond, c)[0] for c in WORST['conds'])
    print(f"worst gaps: Hi->big {bigLeft - hlRight}px, "
          f"Lo->cond {cndLeft - hlRight}px, "
          f"Hi->Lo {LO_BASE - H_HL - HI_BASE}px, big->cond {COND_BASE - H_COND - CUR_BASE}px, "
          f"top {CONTENT_TOP}px, content->day {DAY_TOP - CONTENT_BOT}px, "
          f"day->bottom {122 - DAY_TOP - DAY_H}px")
    assert day_x(0) == MARGIN and day_x(6) + DAY_W == 250 - MARGIN, "day ruler off the margins"
    x0, _, x1, _ = WORD_BOX
    for r in WORST['ranges']:
        f, h, _, _ = tmrw_layout(big, hl, r)
        w = ink(f, r)[1]
        print(f"tomorrow {r}: {h}px font, {w}px of {x1 - x0}px")
        assert w <= x1 - x0, f"tomorrow range {r} overflows the box"


# ----------------------------- word mode art -----------------------------
def render_word(path, word, max_w, max_h):
    """Largest mono rendering of word whose ink fits max_w x max_h -> (w, h, rows)."""
    face = freetype.Face(path)
    best = None
    for pt in range(10, 300):
        face.set_char_size(pt * 64, 0, DPI, 0)
        pen, ink = 0, []
        for c in word:
            face.load_char(c, freetype.FT_LOAD_TARGET_MONO | freetype.FT_LOAD_RENDER)
            g = face.glyph; b = g.bitmap
            for y in range(b.rows):
                for x in range(b.width):
                    if (b.buffer[y * b.pitch + (x >> 3)] >> (7 - (x & 7))) & 1:
                        ink.append((pen + g.bitmap_left + x, -g.bitmap_top + y))
            pen += g.advance.x >> 6
        xs = [p[0] for p in ink]; ys = [p[1] for p in ink]
        w, h = max(xs) - min(xs) + 1, max(ys) - min(ys) + 1
        if w > max_w or h > max_h:
            break
        best = (w, h, {(x - min(xs), y - min(ys)) for x, y in ink})
    w, h, on = best
    return w, h, [[(x, y) in on for x in range(w)] for y in range(h)]


def _lens(x0, x1, top, bot, inset, flip):
    """Wayfarer lens: straight top, tapered sides, superellipse bottom."""
    ym = top + (bot - top) * 0.30
    cx = (x0 + x1) / 2; rx = (x1 - x0) / 2 - inset; ry = bot - ym
    pts = [(x0, top), (x1, top), (x1 - inset, ym)]
    for i in range(61):
        t = math.pi * i / 60
        c, sn = math.cos(t), math.sin(t)
        pts.append((cx + rx * math.copysign(abs(c) ** (2 / 2.6), c), ym + ry * abs(sn) ** (2 / 2.6)))
    pts.append((x0 + inset, ym))
    return [(x0 + x1 - x, y) for x, y in pts] if flip else pts


def render_shades(w):
    """Sunglasses silhouette, drawn 8x oversampled then thresholded -> (w, h, rows)."""
    h = round(w / 2.5); k = 8; W, H = w * k, h * k
    im = Image.new("L", (W, H), 0); d = ImageDraw.Draw(im)
    bar = int(H * 0.17)
    d.rounded_rectangle((0, 0, W - 1, bar), radius=bar // 3, fill=255)      # brow bar
    gap = int(W * 0.13); lw = (W - gap) // 2
    d.polygon(_lens(0, lw, bar * 0.5, H - 1, lw * 0.07, False), fill=255)
    d.polygon(_lens(lw + gap, W - 1, bar * 0.5, H - 1, lw * 0.07, True), fill=255)
    im = im.resize((w, h), Image.LANCZOS)
    px = im.load()
    return w, h, [[px[x, y] >= 128 for x in range(w)] for y in range(h)]


def emit_art(name, art):
    """Adafruit drawBitmap format: rows MSB-first, each row padded to a byte."""
    w, h, rows = art
    out = bytearray()
    for r in rows:
        for i in range(0, w, 8):
            v = 0
            for k in range(8):
                v = (v << 1) | (1 if i + k < w and r[i + k] else 0)
            out.append(v)
    return (f"static const uint8_t {name}Bits[] PROGMEM = {{\n  "
            + ", ".join(f"0x{b:02X}" for b in out) + "\n};", out)


def build_words():
    """Write WORDS_HEADER and return {label: art} for the preview."""
    x0, y0, x1, y1 = WORD_BOX
    arts = {wd: render_word(FONT_WORD, wd, x1 - x0, y1 - y0) for wd in WORDS}
    arts["SHADES"] = render_shades(SHADES_W)
    lines = ["#pragma once", "#include <Arduino.h>", "",
             "// Generated by tools/weather_design.py - do not edit by hand.",
             f"// Each art is centred in the box x={x0}..{x1}, y={y0}..{y1}.", ""]
    names = WORDS + ["SHADES"]
    for n in names:
        code, _ = emit_art("Art" + n.title(), arts[n]); lines.append(code)
    lines += ["", "struct WordArt { uint16_t w, h; const uint8_t* bits; };", ""]
    for n in names:
        w, h, _ = arts[n]
        lines.append(f"static const WordArt ART_{n} = {{ {w}, {h}, Art{n.title()}Bits }};")
    lines += ["", f"static const int WORD_BOX_X0 = {x0}, WORD_BOX_Y0 = {y0}, "
              f"WORD_BOX_X1 = {x1}, WORD_BOX_Y1 = {y1};", ""]
    with open(WORDS_HEADER, "w") as fh:
        fh.write("\n".join(lines))
    print(f"wrote {WORDS_HEADER}  (" + ", ".join(f"{n} {arts[n][0]}x{arts[n][1]}" for n in names) + ")")
    return arts


def paste_art(img, art, fill=0):
    """Centre art in WORD_BOX, same maths as drawWordArt() in the firmware."""
    x0, y0, x1, y1 = WORD_BOX
    w, h, rows = art
    ox = x0 + (x1 - x0 - w) // 2; oy = y0 + (y1 - y0 - h) // 2
    px = img.load()
    for y in range(h):
        for x in range(w):
            if rows[y][x]:
                px[ox + x, oy + y] = fill


def night(img):
    """Night theme: the firmware swaps ink and paper (setTheme()), so a night
    screen is the day screen with every pixel inverted."""
    return img.point(lambda x: 236 if x < 128 else 0)


def main():
    big  = convert(FONT_BIG,  STRUCTS['big'],  H_BIG)
    hl   = convert(FONT_HL,   STRUCTS['hl'],   H_HL)
    cond = convert(FONT_COND, STRUCTS['cond'], H_COND)

    with open(HEADER, "w") as fh:
        fh.write("#pragma once\n#include <Adafruit_GFX.h>\n\n"
                 + emit(big) + "\n\n" + emit(hl) + "\n\n" + emit(cond) + "\n")
    print(f"wrote {HEADER}  (BIG {len(big['bitmaps'])}B, HL {len(hl['bitmaps'])}B, "
          f"COND {len(cond['bitmaps'])}B)")
    check(big, hl, cond)
    build_days()

    img = Image.new("L", (250, 122), 236); d = ImageDraw.Draw(img)   # e-ink gray
    draw_day(d, PREVIEW['day'])
    blit(d, hl,   PREVIEW['hi'], LEFT_X, HI_BASE)
    blit(d, hl,   PREVIEW['lo'], LEFT_X, LO_BASE)
    blit(d, big,  PREVIEW['cur'],  right_pen(big,  PREVIEW['cur']),  CUR_BASE)
    blit(d, cond, PREVIEW['cond'], right_pen(cond, PREVIEW['cond']), COND_BASE)
    img = img.point(lambda x: 0 if x < 128 else 236)
    img.resize((1000, 488), Image.NEAREST).save(PREVIEW_PNG)
    night(img).resize((1000, 488), Image.NEAREST).save(PREVIEW_NIGHT_PNG)
    print(f"wrote {PREVIEW_PNG} and {PREVIEW_NIGHT_PNG}  (open them to see the layout)")

    # Tomorrow screen: only ever shown at night, so preview it in the night
    # theme. One screen per PREVIEW range, stacked (big font, then the fallback).
    rngs = PREVIEW['tmrw']
    sheet = Image.new("L", (1000, len(rngs) * 498), 255)
    for i, r in enumerate(rngs):
        img = Image.new("L", (250, 122), 236); d = ImageDraw.Draw(img)
        draw_day(d, PREVIEW['tmrw_day'])
        f, _, lb, rb = tmrw_layout(big, hl, r)
        blit(d, cond, "Tomorrow", centre_pen(cond, "Tomorrow"), lb)
        blit(d, f, r, centre_pen(f, r), rb)
        img = img.point(lambda x: 0 if x < 128 else 236)
        sheet.paste(night(img).resize((1000, 488), Image.NEAREST), (0, i * 498))
    sheet.save(PREVIEW_TMRW_PNG)
    print(f"wrote {PREVIEW_TMRW_PNG}")

    # Word mode: every word on its own screen, stacked into one sheet,
    # day on the left and night on the right.
    arts = build_words()
    names = WORDS + ["SHADES"]
    sheet = Image.new("L", (1010, len(names) * 254), 255)
    for i, n in enumerate(names):
        scr = Image.new("L", (250, 122), 236); sd = ImageDraw.Draw(scr)
        draw_day(sd, PREVIEW['day'])
        paste_art(scr, arts[n])
        sheet.paste(scr.resize((500, 244), Image.NEAREST), (0, i * 254))
        sheet.paste(night(scr).resize((500, 244), Image.NEAREST), (510, i * 254))
    sheet.save(WORDS_PNG)
    print(f"wrote {WORDS_PNG}")


if __name__ == "__main__":
    main()
