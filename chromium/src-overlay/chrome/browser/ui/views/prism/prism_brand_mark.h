// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_VIEWS_PRISM_PRISM_BRAND_MARK_H_
#define CHROME_BROWSER_UI_VIEWS_PRISM_PRISM_BRAND_MARK_H_

#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/image/image_skia.h"

namespace gfx {
class Canvas;
}

namespace prism {

// Paints the LogiBricks mark scaled into |bounds| (aspect preserved, centered).
// Geometry derives from chromium/branding/icons/generate_icons.py: 576x576
// grid, 158px bricks (r=21) at (0,0) (0,209) (0,418) (209,418) (418,418) and
// the dot circle (497,79) r=71. Bricks take |brick_color| (theme-aware);
// the dot is always the brand terracotta #C87858.
void PaintLogiBricksMark(gfx::Canvas* canvas,
                         const gfx::RectF& bounds,
                         SkColor brick_color);

// Rasterizes the mark at |size_px| (1x + 2x representations).
gfx::ImageSkia CreateLogiBricksMarkImage(int size_px, SkColor brick_color);

// Same, with a dark circular count badge (white numeral) composited at the
// icon's bottom-right — the running-background-agents count (recon §5).
// |count| must be >= 1 (callers hide the button/badge at 0 instead).
gfx::ImageSkia CreateLogiBricksMarkImageWithBadge(int size_px,
                                                  SkColor brick_color,
                                                  int count);

}  // namespace prism

#endif  // CHROME_BROWSER_UI_VIEWS_PRISM_PRISM_BRAND_MARK_H_
