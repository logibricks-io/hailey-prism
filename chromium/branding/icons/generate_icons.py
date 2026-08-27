#!/usr/bin/env python3
"""Generate the Prism icon set from the LogiBricks brand motif.

Source of truth for the motif: the LogiBricks.AI sketch — an L-shaped grid of
near-black rounded bricks with a single terracotta dot. The browser icon puts
that motif on a dark macOS squircle tile: ivory bricks, terracotta accent.

Regenerate with:

    python3 chromium/branding/icons/generate_icons.py

Outputs (written next to this script under out/):
    out/app.icns            macOS app icon (iconutil)
    out/Assets.car          compiled asset catalog with AppIcon (actool)
    out/product_logo_32.png 32px in-UI product logo
    out/preview-1024.png    full-size render for review

Requires Pillow plus macOS built-ins (iconutil, actool).
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(HERE, "out")

TILE = 1024
SQUIRCLE_RADIUS = 225          # macOS-style continuous corner
TILE_TOP = (0x26, 0x26, 0x2B)  # vertical gradient, top
TILE_BOTTOM = (0x14, 0x14, 0x17)
BRICK = (0xED, 0xED, 0xE8)     # ivory
DOT = (0xC8, 0x7B, 0x5D)       # terracotta accent

# Motif geometry, normalized from the sketch (640x638): brick size 140 with
# 50px gaps (pitch 190); the L-grid cells below; terracotta dot at top-right.
_BRICK = 140
_CELLS = [  # (col, row) of the ivory bricks
    (0, 0),
    (0, 1),
    (0, 2),
    (1, 2),
    (2, 2),
]
_DOT_CELL = (2, 0)
_CELL_PITCH = 190
_DOT_RADIUS = 70
_BRICK_RADIUS = 28             # 20% of the brick size, as in the sketch

MOTIF_W = 2 * _CELL_PITCH + _BRICK
MOTIF_H = 2 * _CELL_PITCH + _BRICK

ICONSET_ENTRIES = [
    ("16x16", 16), ("16x16@2x", 32), ("32x32", 32), ("32x32@2x", 64),
    ("128x128", 128), ("128x128@2x", 256), ("256x256", 256),
    ("256x256@2x", 512), ("512x512", 512), ("512x512@2x", 1024),
]


def _vertical_gradient(size, top, bottom):
    base = Image.new("RGBA", (size, size))
    px = base.load()
    for y in range(size):
        t = y / (size - 1)
        row = tuple(round(top[i] + (bottom[i] - top[i]) * t) for i in range(3)) + (255,)
        for x in range(size):
            px[x, y] = row
    return base


def _squircle_mask(size, radius):
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        [0, 0, size - 1, size - 1], radius=radius, fill=255)
    return mask


def _draw_motif(canvas, origin, scale):
    """Draw the brick-L + dot motif. origin=(x, y) top-left of the motif,
    scale = canvas px per sketch px."""
    d = ImageDraw.Draw(canvas)
    ox, oy = origin
    brick = _BRICK * scale
    pitch = _CELL_PITCH * scale
    radius = _BRICK_RADIUS * scale
    for col, row in _CELLS:
        x0 = ox + col * pitch
        y0 = oy + row * pitch
        d.rounded_rectangle([x0, y0, x0 + brick, y0 + brick],
                            radius=radius, fill=BRICK + (255,))
    dot_r = _DOT_RADIUS * scale
    cx = ox + _DOT_CELL[0] * pitch + brick / 2
    cy = oy + _DOT_CELL[1] * pitch + brick / 2
    d.ellipse([cx - dot_r, cy - dot_r, cx + dot_r, cy + dot_r],
              fill=DOT + (255,))


def render(px):
    """Full app icon at px pixels: dark squircle tile with the motif."""
    canvas = Image.new("RGBA", (px, px), (0, 0, 0, 0))
    scale = px / TILE

    tile = _vertical_gradient(px, TILE_TOP, TILE_BOTTOM)
    canvas.paste(tile, (0, 0), _squircle_mask(px, int(SQUIRCLE_RADIUS * scale)))

    # Subtle top-edge inner highlight for depth.
    hl = Image.new("RGBA", (px, px), (0, 0, 0, 0))
    ImageDraw.Draw(hl).rounded_rectangle(
        [1, 1, px - 2, px - 2], radius=int(SQUIRCLE_RADIUS * scale),
        outline=(255, 255, 255, 28), width=max(1, int(3 * scale)))
    canvas.alpha_composite(hl)

    motif_w = MOTIF_W * scale
    motif_h = MOTIF_H * scale
    _draw_motif(canvas, ((px - motif_w) / 2, (px - motif_h) / 2), scale)
    return canvas


def render_product_logo(px=32):
    """Small in-UI logo: motif only, tight, no tile."""
    canvas = Image.new("RGBA", (px, px), (0, 0, 0, 0))
    scale = (px * 0.92) / MOTIF_W
    _draw_motif(canvas, ((px - MOTIF_W * scale) / 2,
                         (px - MOTIF_H * scale) / 2), scale)
    return canvas


def build_appiconset(directory):
    """Write an actool-compatible AppIcon.appiconset into `directory`."""
    set_dir = os.path.join(directory, "AppIcon.appiconset")
    os.makedirs(set_dir)
    images = []
    for name, pixels in ICONSET_ENTRIES:
        size, scale = (name.split("@")[0], "2x") if "@" in name else (name, "1x")
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

    render(TILE).save(os.path.join(OUT_DIR, "preview-1024.png"))

    with tempfile.TemporaryDirectory() as tmp:
        # app.icns via the classic .iconset + iconutil route.
        iconset = os.path.join(tmp, "app.iconset")
        os.makedirs(iconset)
        for name, pixels in ICONSET_ENTRIES:
            render(pixels).save(os.path.join(iconset, f"icon_{name}.png"))
        subprocess.run(["iconutil", "-c", "icns", iconset,
                        "-o", os.path.join(OUT_DIR, "app.icns")], check=True)

        # Assets.car via actool (AppIcon only).
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
    render_product_logo(32).save(os.path.join(OUT_DIR, "product_logo_32.png"))

    for name in sorted(os.listdir(OUT_DIR)):
        print(f"  wrote {os.path.join('out', name)} "
              f"({os.path.getsize(os.path.join(OUT_DIR, name))} bytes)")


if __name__ == "__main__":
    sys.exit(main())
