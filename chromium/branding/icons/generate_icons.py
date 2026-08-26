#!/usr/bin/env python3
"""Generate the Prism placeholder icon set (prism/spectrum motif).

No design assets exist yet; this renders a deliberate placeholder: a rounded
"app icon" tile with a vertical indigo gradient, a white prism triangle
center-left taking a white beam, and a spectrum fan exiting to the right.
Good enough to look like a real product; regenerate with:

    python3 chromium/branding/icons/generate_icons.py

Outputs (written next to this script under out/):
    out/app.icns            macOS app icon (iconutil)
    out/Assets.car          compiled asset catalog with AppIcon (actool)
    out/product_logo_32.png 32px in-UI product logo

Requires Pillow plus macOS built-ins (iconutil, actool).
"""

import json
import math
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(HERE, "out")

# Spectrum hues (red through violet) for the fan.
SPECTRUM = [
    (244, 67, 54),
    (255, 152, 0),
    (255, 235, 59),
    (76, 175, 80),
    (33, 150, 243),
    (63, 81, 181),
    (156, 39, 176),
]


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def render(size):
    """Render the icon at the given pixel size."""
    s = size
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))

    # Tile: vertical gradient indigo -> deep violet, clipped to the macOS
    # rounded-rect silhouette (~22.4% corner radius, 82.4% content box).
    tile = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    draw = ImageDraw.Draw(tile)
    top = (49, 27, 146)  # deep indigo
    bottom = (74, 20, 140)  # violet
    for y in range(s):
        draw.line([(0, y), (s, y)], fill=lerp(top, bottom, y / (s - 1)) + (255,))

    mask = Image.new("L", (s, s), 0)
    mdraw = ImageDraw.Draw(mask)
    inset = s * 0.088
    radius = s * 0.224
    mdraw.rounded_rectangle([inset, inset, s - inset, s - inset], radius=radius, fill=255)
    img.paste(tile, (0, 0), mask)

    g = ImageDraw.Draw(img)

    # Prism triangle, centered slightly left of middle.
    cx, cy = s * 0.46, s * 0.52
    tri = s * 0.30
    apex = (cx, cy - tri * 0.62)
    left = (cx - tri * 0.58, cy + tri * 0.48)
    right = (cx + tri * 0.58, cy + tri * 0.48)

    # Incoming white beam (from the left edge into the triangle).
    beam_w = max(1.0, s * 0.018)
    g.line([(s * 0.10, cy - tri * 0.10), (apex[0] - tri * 0.10, apex[1] + tri * 0.30)],
           fill=(255, 255, 255, 235), width=int(beam_w))

    # Spectrum fan out of the triangle's right edge.
    fan_origin = (cx + tri * 0.10, cy + tri * 0.02)
    fan_start = math.radians(-38)
    fan_end = math.radians(30)
    fan_len = s * 0.34
    for i, color in enumerate(SPECTRUM):
        angle = fan_start + (fan_end - fan_start) * (i / (len(SPECTRUM) - 1))
        end = (fan_origin[0] + fan_len * math.cos(angle),
               fan_origin[1] + fan_len * math.sin(angle))
        g.line([fan_origin, end], fill=color + (255,), width=int(beam_w))

    # Triangle outline on top.
    outline_w = max(1.0, s * 0.022)
    g.polygon([apex, right, left], outline=(255, 255, 255, 255))
    g.line([apex, right], fill=(255, 255, 255, 255), width=int(outline_w))
    g.line([right, left], fill=(255, 255, 255, 255), width=int(outline_w))
    g.line([left, apex], fill=(255, 255, 255, 255), width=int(outline_w))

    return img


ICONSET_ENTRIES = [
    ("16x16", 16), ("16x16@2x", 32), ("32x32", 32), ("32x32@2x", 64),
    ("128x128", 128), ("128x128@2x", 256), ("256x256", 256),
    ("256x256@2x", 512), ("512x512", 512), ("512x512@2x", 1024),
]


def build_appiconset(directory):
    """Write an actool-compatible AppIcon.appiconset into `directory`."""
    set_dir = os.path.join(directory, "AppIcon.appiconset")
    os.makedirs(set_dir)
    images = []
    for name, pixels in ICONSET_ENTRIES:
        if "@" in name:
            size, scale = name.split("@")[0], "2x"
        else:
            size, scale = name, "1x"
        filename = f"appicon_{pixels}.png"
        render(pixels).save(os.path.join(set_dir, filename))
        images.append({"idiom": "mac", "size": size, "scale": scale,
                       "filename": filename})
    with open(os.path.join(set_dir, "Contents.json"), "w") as f:
        json.dump({"images": images,
                   "info": {"author": "xcode", "version": 1}}, f)


def main():
    if os.path.isdir(OUT_DIR):
        shutil.rmtree(OUT_DIR)
    os.makedirs(OUT_DIR)

    with tempfile.TemporaryDirectory() as tmp:
        # app.icns via the classic .iconset + iconutil route.
        iconset = os.path.join(tmp, "app.iconset")
        os.makedirs(iconset)
        for name, pixels in ICONSET_ENTRIES:
            render(pixels).save(os.path.join(iconset, f"icon_{name}.png"))
        subprocess.run(["iconutil", "-c", "icns", iconset,
                        "-o", os.path.join(OUT_DIR, "app.icns")], check=True)

        # Assets.car via actool (AppIcon only; the stock catalog also carries
        # ancillary icons we do not need for a v1 brand).
        xcassets = os.path.join(tmp, "Assets.xcassets")
        os.makedirs(xcassets)
        build_appiconset(xcassets)
        with open(os.path.join(xcassets, "Contents.json"), "w") as f:
            json.dump({"info": {"author": "xcode", "version": 1}}, f)
        car_out = os.path.join(tmp, "car")
        os.makedirs(car_out)
        subprocess.run([
            "actool", "--compile", car_out,
            "--platform", "macosx",
            "--minimum-deployment-target", "14.0",
            "--app-icon", "AppIcon",
            "--output-partial-info-plist", os.path.join(tmp, "assetcatalog-info.plist"),
            xcassets,
        ], check=True)
        shutil.copyfile(os.path.join(car_out, "Assets.car"),
                        os.path.join(OUT_DIR, "Assets.car"))

    # In-UI product logo (chrome://version, about surfaces).
    render(32).save(os.path.join(OUT_DIR, "product_logo_32.png"))

    for name in sorted(os.listdir(OUT_DIR)):
        print(f"  wrote {os.path.join('out', name)} "
              f"({os.path.getsize(os.path.join(OUT_DIR, name))} bytes)")


if __name__ == "__main__":
    sys.exit(main())
