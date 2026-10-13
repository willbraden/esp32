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
FONT_DAY  = FONT_MAIN  # weekday letters in the bar

# Glyph height in pixels for each element (bigger = bigger text).
# Sized so the WORST case below keeps >= 8px between neighbours (width-bound:
# a 3-digit Hi next to a 3-digit current temp is exactly 8px apart; JetBrains
# Mono's digits are wider than Arial's, so the big temp dropped from 59 to 57).
H_BIG  = 57   # current temp
H_HL   = 37   # Hi / Lo
H_COND = 17   # condition word
H_DAY  = 11   # weekday letters (cap height)

# Layout positions (must match the LAYOUT KNOBS in weather_main.cpp).
# Screen is 250 wide x 122 tall. y values are text BASELINES.
# Both columns are 84px tall and centred: tops at y=19, bottoms at y=103.
BAR_W     = 19       # inverted weekday bar on the left edge (full height)
DAY_GAP   = 8        # vertical gap between stacked weekday letters
LEFT_X    = BAR_W + 8  # Hi/Lo left edge
RIGHT_X   = 250 - 8  # right edge that the temp + condition align to
HI_BASE   = 56       # Hi baseline   (top aligned with the big temp's top)
LO_BASE   = 103      # Lo baseline   (aligned with the condition baseline)
CUR_BASE  = 76       # current-temp baseline (top = 76 - 57 = 19, level with Hi)
COND_BASE = 103      # condition baseline

# ---- word mode (USER SW toggles between the numbers screen and this) ----
# Each word is rendered as its own bitmap, scaled to the largest size that fits
# the box right of the bar (so short words like HOT come out bigger than CHILLY).
FONT_WORD  = FONT_MAIN
WORD_BOX   = (BAR_W + 8, 8, 250 - 8, 122 - 8)   # x0, y0, x1, y1 (8px margins)
WORDS      = ["SNOW", "RAIN", "FREEZE", "COLD", "CHILLY", "HOT", "HELL"]
SHADES_W   = 190      # sunglasses icon width (height follows its 2.5:1 shape)
WORDS_HEADER = "src/weather/weather_words.h"
WORDS_PNG    = "tools/preview_words.png"

# What to show in the preview (change to test different values / longest words).
PREVIEW = dict(hi="103", lo="74", cur="102", cond="Clear", day="MON")
# Worst cases checked for collisions on every run.
WORST = dict(nums=["108", "-18"], conds=["Cloudy", "Storm", "Clear", "Rain", "Snow", "Fog"],
             days=["MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"])
# ========================================================================

DPI = 141                       # matches Adafruit fontconvert
FIRST, LAST = 0x20, 0x7E        # printable ASCII
STRUCTS = dict(big="WeatherBig", hl="WeatherHL", cond="WeatherCond", day="WeatherDay")
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


def day_layout(day, f):
    """(penx, baseline) per letter: each centred in the bar, stack centred vertically.
    Mirrors drawDayBar() in weather_main.cpp."""
    total = len(day) * H_DAY + (len(day) - 1) * DAY_GAP
    top = (122 - total) // 2
    out = []
    for i, c in enumerate(day):
        g = f['glyphs'][ord(c) - FIRST]
        out.append(((BAR_W - g['w']) // 2 - g['xoff'], top + i * (H_DAY + DAY_GAP) + H_DAY))
    return out


def check(big, hl, cond, day):
    """Print the tightest gaps for the worst-case strings."""
    hlRight = max(LEFT_X + sum(ink(hl, n)) for n in WORST['nums'])      # ink right edge
    bigLeft = min(right_pen(big, n) + ink(big, n)[0] for n in WORST['nums'])
    cndLeft = min(right_pen(cond, c) + ink(cond, c)[0] for c in WORST['conds'])
    wDay = max(day['glyphs'][ord(c) - FIRST]['w'] for d in WORST['days'] for c in d)
    print(f"worst gaps: Hi->big {bigLeft - hlRight}px, "
          f"Lo->cond {cndLeft - hlRight}px, "
          f"Hi->Lo {LO_BASE - H_HL - HI_BASE}px, big->cond {COND_BASE - H_COND - CUR_BASE}px, "
          f"day letter margin {(BAR_W - wDay) / 2:.1f}px")


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


def main():
    big  = convert(FONT_BIG,  STRUCTS['big'],  H_BIG)
    hl   = convert(FONT_HL,   STRUCTS['hl'],   H_HL)
    cond = convert(FONT_COND, STRUCTS['cond'], H_COND)
    day  = convert(FONT_DAY,  STRUCTS['day'],  H_DAY)

    with open(HEADER, "w") as fh:
        fh.write("#pragma once\n#include <Adafruit_GFX.h>\n\n"
                 + emit(big) + "\n\n" + emit(hl) + "\n\n" + emit(cond) + "\n\n"
                 + emit(day) + "\n")
    print(f"wrote {HEADER}  (BIG {len(big['bitmaps'])}B, HL {len(hl['bitmaps'])}B, "
          f"COND {len(cond['bitmaps'])}B, DAY {len(day['bitmaps'])}B)")
    check(big, hl, cond, day)

    img = Image.new("L", (250, 122), 236); d = ImageDraw.Draw(img)   # e-ink gray
    d.rectangle((0, 0, BAR_W - 1, 121), fill=0)
    for c, (x, y) in zip(PREVIEW['day'], day_layout(PREVIEW['day'], day)):
        blit(d, day, c, x, y, fill=236)
    blit(d, hl,   PREVIEW['hi'], LEFT_X, HI_BASE)
    blit(d, hl,   PREVIEW['lo'], LEFT_X, LO_BASE)
    blit(d, big,  PREVIEW['cur'],  right_pen(big,  PREVIEW['cur']),  CUR_BASE)
    blit(d, cond, PREVIEW['cond'], right_pen(cond, PREVIEW['cond']), COND_BASE)
    img.point(lambda x: 0 if x < 128 else 236).resize((1000, 488), Image.NEAREST).save(PREVIEW_PNG)
    print(f"wrote {PREVIEW_PNG}  (open it to see the layout)")

    # Word mode: every word on its own screen, stacked into one sheet.
    arts = build_words()
    names = WORDS + ["SHADES"]
    sheet = Image.new("L", (500, len(names) * 254), 255)
    for i, n in enumerate(names):
        scr = Image.new("L", (250, 122), 236); sd = ImageDraw.Draw(scr)
        sd.rectangle((0, 0, BAR_W - 1, 121), fill=0)
        for c, (x, y) in zip(PREVIEW['day'], day_layout(PREVIEW['day'], day)):
            blit(sd, day, c, x, y, fill=236)
        paste_art(scr, arts[n])
        sheet.paste(scr.resize((500, 244), Image.NEAREST), (0, i * 254))
    sheet.save(WORDS_PNG)
    print(f"wrote {WORDS_PNG}")


if __name__ == "__main__":
    main()
