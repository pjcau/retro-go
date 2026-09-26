#!/usr/bin/env python3
"""Import launcher art for systems the default theme has none for.

Source: es-theme-gbz35 (https://github.com/rxbrad/es-theme-gbz35), the theme
retro-go already credits for its background images. For each system:

  background_<tab>.png  320x240  the theme's background.png, downscaled
  banner_<tab>.png      272x24   the theme's system.svg logo, 22 px high on the
                                 launcher's transparent colour (magenta 0xF81F)
  logo_<tab>.png        46x50    the system icon of background.png, cropped

Only files that do not exist yet are written (pass --force to overwrite).
The SVG logos are rendered with a headless Chromium (ImageMagick's SVG
renderer draws some of them as black boxes).

usage: tools/import_gbz35_art.py <es-theme-gbz35 checkout> <chrome-headless-shell>
then:  python3 tools/gen_images.py
"""
import os, subprocess, sys, tempfile
from PIL import Image

# Logos drawn in plain black vanish on the launcher's dark header: paint them white.
BLACK_LOGOS = {"a26"}

# launcher tab short name -> es-theme-gbz35 folder
SYSTEMS = {
    "arcade": "arcade",
    "duke3d": "pc",  # a DOS game; the theme has no Duke Nukem entry
    "sg1": "sg-1000",
    "a26": "atari2600",
    "gba": "gba",
    "msx": "msx",
    "ngp": "ngp",
}
THEME_DIR = os.path.join(os.path.dirname(__file__), "..", "themes", "default")
MAGENTA = (255, 0, 255)


def render_svg(chrome, svg_path, width=1200):
    with tempfile.TemporaryDirectory() as tmp:
        html = os.path.join(tmp, "r.html")
        png = os.path.join(tmp, "r.png")
        with open(html, "w") as f:
            f.write('<html><body style="margin:0;background:transparent">'
                    '<img src="file://%s" style="width:%dpx"></body></html>' % (os.path.abspath(svg_path), width))
        subprocess.run([chrome, "--headless", "--disable-gpu", "--no-sandbox",
                        "--default-background-color=00000000", "--window-size=%d,%d" % (width, width),
                        "--screenshot=" + png, "file://" + html],
                       check=True, capture_output=True)
        im = Image.open(png).convert("RGBA")
        return im.crop(im.getbbox())


def to_palette(im, colors):
    return im.convert("RGB").quantize(colors=colors, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)


def banner(logo, black_to_white=False):
    """Logo 22 px high, left aligned, hard alpha edge on the transparent colour."""
    h = 22
    w = min(268, round(logo.width * h / logo.height))
    h = min(h, round(logo.height * w / logo.width))
    logo = logo.resize((w, h), Image.LANCZOS)
    out = Image.new("RGB", (272, 24), MAGENTA)
    for y in range(h):
        for x in range(w):
            r, g, b, a = logo.getpixel((x, y))
            if a >= 128:
                if black_to_white and r + g + b < 120:
                    r, g, b = 255, 255, 255
                if (r, g, b) == MAGENTA:
                    b -= 1  # never the transparent key
                out.putpixel((2 + x, (24 - h) // 2 + y), (r, g, b))
    pal = out.quantize(colors=32, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    return pal


def logo_from_background(bg):
    """The theme draws the system (cabinet, handheld, controller...) big in the
    background, a darker shade on a flat colour. The logo is that icon as a
    light card like the default theme's logos: the 46:50 window with the most
    icon pixels, the icon shade on white, a grey 1 px frame."""
    bg = bg.convert("RGB").resize((320, 240), Image.LANCZOS)
    colors = sorted(bg.getcolors(320 * 240), reverse=True)
    base = colors[0][1]
    dist = lambda p: sum(abs(a - b) for a, b in zip(p, base))
    icon = next(c for n, c in colors if dist(c) > 30)
    best, best_score = None, -1
    ch, cw = 240, round(240 * 44 / 48)
    for x in range(0, 320 - cw + 1, 8):
        box = (x, 0, x + cw, ch)
        score = sum(1 for p in bg.crop(box).getdata() if dist(p) > 30)
        if score > best_score:
            best, best_score = box, score
    crop = bg.crop(best).resize((44, 48), Image.LANCZOS)
    full = max(1, dist(icon))
    card = Image.new("RGB", (46, 50), (160, 160, 160))
    for y in range(48):
        for x in range(44):
            t = min(1.0, dist(crop.getpixel((x, y))) / full)
            card.putpixel((1 + x, 1 + y), tuple(round(255 * (1 - t) + c * t) for c in icon))
    return to_palette(card, 16)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    theme, chrome = sys.argv[1], sys.argv[2]
    force = "--force" in sys.argv
    for tab, folder in SYSTEMS.items():
        src = os.path.join(theme, folder)
        bg = Image.open(os.path.join(src, "background.png"))
        jobs = {
            "background_%s.png" % tab: lambda: to_palette(bg.resize((320, 240), Image.LANCZOS), 16),
            "banner_%s.png" % tab: lambda: banner(render_svg(chrome, os.path.join(src, "system.svg")), tab in BLACK_LOGOS),
            "logo_%s.png" % tab: lambda: logo_from_background(bg),
        }
        for name, make in jobs.items():
            path = os.path.join(THEME_DIR, name)
            if os.path.exists(path) and not force:
                continue
            make().save(path, optimize=True)
            print("wrote", os.path.relpath(path))


if __name__ == "__main__":
    main()
