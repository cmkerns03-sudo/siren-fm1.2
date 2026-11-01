#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The Clipper logo: a clipper under sail inside a 270-degree dial (the knobs of the UI), in the line of SLOOP's
logo: the sloop's one triangular sail becomes four, a jib and three fore-and-aft sails filled by the wind, one per
track colour (orange, yellow, green, blue), three raked masts on a long low hull with a sharp bow, and a geometric
monoline wordmark "clipper" (SLOOP's).

  tools/gen_logo.py OUT.h          firmware boot splash (RLE, 16-colour palette, RGB565)
  tools/gen_logo.py --assets DIR   clipper-logo.svg/png, clipper-icon.svg/png, clipper-splash.png
"""
import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw

BLUE, GREEN, YELLOW, ORANGE = (40, 124, 255), (30, 204, 112), (255, 198, 24), (255, 98, 26)
WHITE, BLACK = (242, 242, 242), (0, 0, 0)
HEX = lambda c: "#%02X%02X%02X" % c
XH, SW = 72, 12                                  # wordmark x-height, stroke (units: the icon is 240 x 240)


class Pen:
    """round-capped strokes and fills, drawn with PIL (k: scale, ox/oy: offset) or written as SVG"""

    def __init__(self, d=None, k=1.0, ox=0.0, oy=0.0):
        self.d, self.k, self.ox, self.oy, self.svg = d, k, ox, oy, []

    def P(self, x, y):
        return self.ox + x * self.k, self.oy + y * self.k

    def dot(self, x, y, w, col):
        if self.d:
            (X, Y), r = self.P(x, y), w * self.k / 2
            self.d.ellipse([X - r, Y - r, X + r, Y + r], fill=col)

    def line(self, x0, y0, x1, y1, w=SW, col=WHITE):
        if self.d:
            self.d.line([self.P(x0, y0), self.P(x1, y1)], fill=col, width=max(1, round(w * self.k)))
            self.dot(x0, y0, w, col)
            self.dot(x1, y1, w, col)
        else:
            self.svg.append(f'<line x1="{x0:.2f}" y1="{y0:.2f}" x2="{x1:.2f}" y2="{y1:.2f}" stroke="{HEX(col)}" '
                            f'stroke-width="{w}" stroke-linecap="round"/>')

    def arc(self, cx, cy, r, a0, a1, w=SW, col=WHITE, caps=True):
        """an arc of radius r (the stroke's centre line), a0 -> a1 degrees clockwise on screen"""
        if self.d:
            (X, Y), R, W = self.P(cx, cy), (r + w / 2) * self.k, max(1, round(w * self.k))
            self.d.arc([X - R, Y - R, X + R, Y + R], start=a0, end=a1, fill=col, width=W)
            if caps:
                for a in (a0, a1):
                    self.dot(cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a)), w, col)
        else:
            p = (cx + r * math.cos(math.radians(a0)), cy + r * math.sin(math.radians(a0)))
            q = (cx + r * math.cos(math.radians(a1)), cy + r * math.sin(math.radians(a1)))
            if (a1 - a0) % 360 == 0:
                self.svg.append(f'<circle cx="{cx:.2f}" cy="{cy:.2f}" r="{r:.2f}" fill="none" stroke="{HEX(col)}" stroke-width="{w}"/>')
                return
            large = 1 if (a1 - a0) % 360 > 180 else 0
            self.svg.append(f'<path d="M{p[0]:.2f} {p[1]:.2f} A{r:.2f} {r:.2f} 0 {large} 1 {q[0]:.2f} {q[1]:.2f}" fill="none" '
                            f'stroke="{HEX(col)}" stroke-width="{w}" stroke-linecap="{"round" if caps else "butt"}"/>')

    def poly(self, pts, col):
        if self.d:
            self.d.polygon([self.P(x, y) for x, y in pts], fill=col)
        else:
            self.svg.append('<polygon points="' + " ".join(f"{x:.2f},{y:.2f}" for x, y in pts) + f'" fill="{HEX(col)}"/>')


# ---- the icon: the dial (the UI's knobs) round a clipper under sail, the line of SLOOP's logo: its sloop's one
# triangular sail becomes a clipper's four, one per track colour, the leeches filled by the wind; the bow to the left
# (as the sloop sails), a jib out to the bowsprit, three raked masts, a long low hull --------------------------
RAKE = 4                                             # the masts lean aft: their tops this far to the right


def curve(p0, p1, bulge, n=10):
    """points from p0 to p1 bowed out to the right of the travel by bulge (a quadratic)"""
    (x0, y0), (x1, y1) = p0, p1
    mx, my = (x0 + x1) / 2, (y0 + y1) / 2
    dx, dy = x1 - x0, y1 - y0
    L = math.hypot(dx, dy)
    cx, cy = mx - dy / L * bulge * 2, my + dx / L * bulge * 2
    return [((1 - t) ** 2 * x0 + 2 * (1 - t) * t * cx + t * t * x1, (1 - t) ** 2 * y0 + 2 * (1 - t) * t * cy + t * t * y1)
            for t in (i / n for i in range(n + 1))]


class Ship:
    """the ship drawn scaled about the dial's centre (S) and moved (SX, SY), so it sits clear inside the dial"""

    def __init__(self, pen, cx, cy, k=0.86, sx=4, sy=8):
        self.pen, self.cx, self.cy, self.k, self.sx, self.sy = pen, cx, cy, k, sx, sy

    def T(self, x, y):
        return self.cx + (x - self.cx) * self.k + self.sx, self.cy + (y - self.cy) * self.k + self.sy

    def line(self, x0, y0, x1, y1, w):
        self.pen.line(*self.T(x0, y0), *self.T(x1, y1), w=w)

    def poly(self, pts, col):
        self.pen.poly([self.T(x, y) for x, y in pts], col)


def icon(pen, ox, oy):
    cx, cy = ox + 120, oy + 120
    pen.arc(cx, cy, 100, 135, 405)
    sh = Ship(pen, cx, cy)
    deck, foot = cy + 52, cy + 40                    # the deck; the sails' feet, over the deck
    masts = [(-38, -66), (2, -88), (40, -60)]        # fore, main, mizzen: x from the centre, top
    tops = []
    for dx, top in masts:
        x0, x1 = cx + dx, cx + dx + RAKE
        sh.line(x0, deck - 2, x1, cy + top, w=7)
        tops.append((x1, cy + top))
    # the sails: luff on the mast (clear of it), foot over the deck, leech bowed by the wind
    widths = [30, 34, 28]
    for (x1, ty), (dx, top), w, col in zip(tops, masts, widths, (YELLOW, GREEN, BLUE)):
        xb = cx + dx + 7                             # the luff at the foot (the mast leans)
        luff_top = (x1 + 7 - 1, ty + 8)
        clew = (xb + w, foot)
        sh.poly([luff_top] + curve(luff_top, clew, 5)[1:] + [(xb, foot)], col)
    fx, fty = tops[0]                                # the jib: from the foremast's head to the bowsprit
    head, tack = (fx - 7, fty + 12), (cx - 90, foot - 4)
    sh.poly([head, (fx - 7, foot), tack] + curve(tack, head, 3)[1:-1], ORANGE)
    sh.line(cx - 70, deck - 8, cx - 96, foot - 6, w=5)    # the bowsprit
    # the hull: long and low, a raised sharp bow forward (the clipper's), a sheer that falls aft, a short counter
    hull = curve((cx - 88, deck - 12), (cx + 78, deck - 3), 2)[:-1] + [(cx + 78, deck - 3), (cx + 72, deck + 11)] + \
        curve((cx + 72, deck + 11), (cx - 58, deck + 15), -2)[1:]
    sh.poly(hull, WHITE)


# ---- the wordmark "clipper": circles of the x-height, verticals, round ends -----------------------
R = XH / 2 - SW / 2                                  # the bowls' centre-line radius
GAP = 16


def word_width():
    return word(Pen(), 0, 0)                          # (a dry run: where the r ends)


def word(pen, ox, oy):
    x, mid, base, w = ox, oy + XH / 2, oy + XH, SW
    pen.arc(x + XH / 2, mid, R, 40, 320)                 # c
    x += XH + GAP
    pen.line(x + w / 2, oy - 32 + w / 2, x + w / 2, base - w / 2)          # l
    x += w + GAP
    pen.line(x + w / 2, oy + w / 2, x + w / 2, base - w / 2)                # i
    pen.dot(x + w / 2, oy - 22, w * 1.1, WHITE) if pen.d else pen.svg.append(
        f'<circle cx="{x + w / 2:.2f}" cy="{oy - 22:.2f}" r="{w * 0.55:.2f}" fill="{HEX(WHITE)}"/>')
    x += w + GAP
    for _ in range(2):                                   # p p
        pen.arc(x + XH / 2, mid, R, 0, 360, caps=False)
        pen.line(x + w / 2, mid, x + w / 2, base + 36 - w / 2)
        x += XH + GAP
    pen.arc(x + XH / 2, mid, R, 40, 360)                 # e: the bowl open at the lower right, its bar
    pen.line(x + w / 2, mid, x + XH - w / 2, mid)
    x += XH + GAP
    pen.line(x + w / 2, oy + w / 2, x + w / 2, base - w / 2)                # r
    pen.arc(x + w / 2 + R, mid, R, 180, 290)
    return x + w / 2 + 2 * R * 0.95 - ox


def render(kind, W, H, k, ss=4):
    im = Image.new("RGB", (W * ss, H * ss), BLACK)
    d = ImageDraw.Draw(im)
    K = k * ss
    if kind == "h":                                      # icon + wordmark side by side
        icon(Pen(d, K, 10 * K, (H * ss - 240 * K) / 2), 0, 0)
        word(Pen(d, K, 270 * K, H * ss / 2 - 36 * K), 0, 0)
    elif kind == "v":                                    # icon over the wordmark (the boot splash)
        icon(Pen(d, K, (W * ss - 240 * K) / 2, 0), 0, 0)
        word(Pen(d, K, (W * ss - word_width() * K) / 2, 270 * K), 0, 0)
    else:
        icon(Pen(d, K, (W * ss - 240 * K) / 2, (H * ss - 240 * K) / 2), 0, 0)
    return im.resize((W, H), Image.LANCZOS)


def svg(kind):
    pen = Pen()
    if kind == "h":
        W, H = 270 + word_width() + 20, 260
        icon(pen, 10, 10)
        sub = Pen()
        word(sub, 270, H / 2 - 36)
        pen.svg += sub.svg
    else:
        W, H = 240, 240
        icon(pen, 0, 0)
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W:.0f} {H}" width="{W:.0f}" height="{H}" role="img" '
            f'aria-label="Clipper">\n<rect width="{W:.0f}" height="{H}" fill="#000"/>\n' + "\n".join(pen.svg) + "\n</svg>\n")


# ---- firmware splash ---------------------------------------------------------------------------
SPLASH_W, SPLASH_H = 240, 188
SPLASH_K = 0.44
assert (270 + 72 + 36 + 6) * SPLASH_K <= SPLASH_H, "the wordmark's p must fit the splash"
assert word_width() * SPLASH_K <= SPLASH_W - 4, "the wordmark must fit the splash"


def rgb565(c):
    r, g, b = c
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def splash_header(path):
    import os
    if os.environ.get("SIREN") == "1":            # SIREN: its own logo (tools/siren/siren-splash.png, from its SVG)
        im = Image.open(Path(__file__).with_name("siren") / "siren-splash.png").convert("RGB").resize((SPLASH_W, SPLASH_H))
    else:
        im = render("v", SPLASH_W, SPLASH_H, SPLASH_K)
    # a fixed palette: black, and each logo colour at 1/3, 2/3 and full (the anti-aliased edges)
    cols = [BLACK]
    for c in (WHITE, BLUE, GREEN, YELLOW, ORANGE):
        cols += [tuple(round(v * f) for v in c) for f in (1 / 3, 2 / 3, 1.0)]
    src = im.tobytes()
    px = []
    cache = {}
    for i in range(0, len(src), 3):
        p = (src[i], src[i + 1], src[i + 2])
        if p not in cache:
            cache[p] = min(range(16), key=lambda k: sum((p[j] - cols[k][j]) ** 2 for j in range(3)))
        px.append(cache[p])
    rle = bytearray()                                 # (run - 1) << 4 | index, runs of 1..16
    i = 0
    while i < len(px):
        j = i
        while j < len(px) and px[j] == px[i] and j - i < 16:
            j += 1
        rle.append(((j - i - 1) << 4) | px[i])
        i = j
    # (the names stay SLOOP_*: firmware/src/splash.c reads them)
    L = ["/* generated by tools/gen_logo.py: the Clipper boot splash */", "#pragma once", "#include <stdint.h>",
         f"#define SLOOP_SPLASH_W {SPLASH_W}", f"#define SLOOP_SPLASH_H {SPLASH_H}",
         "static const uint16_t SLOOP_SPLASH_PAL[16] = {" + ", ".join(f"0x{rgb565(c):04X}" for c in cols) + "};",
         f"static const uint8_t SLOOP_SPLASH_RLE[{len(rle)}] = {{"]
    for k in range(0, len(rle), 24):
        L.append("    " + ", ".join(str(b) for b in rle[k:k + 24]) + ",")
    L.append("};")
    Path(path).write_text("\n".join(L) + "\n")
    print(f"logo: splash {SPLASH_W}x{SPLASH_H}, {len(rle)} B RLE -> {path}")


def assets(out):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    (out / "clipper-logo.svg").write_text(svg("h"))
    (out / "clipper-icon.svg").write_text(svg("i"))
    W = int(270 + word_width() + 20)
    render("h", W * 2, 520, 2.0).save(out / "clipper-logo.png")
    render("i", 512, 512, 512 / 240).save(out / "clipper-icon.png")
    render("v", SPLASH_W, SPLASH_H, SPLASH_K).resize((SPLASH_W * 2, SPLASH_H * 2), Image.NEAREST).save(out / "clipper-splash.png")
    print(f"logo: assets in {out}")


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--assets":
        assets(sys.argv[2])
    elif len(sys.argv) == 2:
        splash_header(sys.argv[1])
    else:
        sys.exit(__doc__)
