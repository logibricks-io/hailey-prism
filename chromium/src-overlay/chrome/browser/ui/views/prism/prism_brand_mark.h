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

// Rasterizes the mark at |size_px| DIP (1x + 2x + 3x representations).
gfx::ImageSkia CreateLogiBricksMarkImage(int size_px, SkColor brick_color);

// Toolbar button geometry (recon §5): ToolbarButton adds the standard
// TOOLBAR_BUTTON insets (7 DIP per side) around the image, so an 18 DIP
// image yields the 32x32 DIP hit target, with the glyph drawn at 16 DIP
// (Chromium's kDefaultIconSize convention) centered inside it.
inline constexpr int kToolbarButtonImageSize = 18;
inline constexpr int kToolbarButtonGlyphSize = 16;

// Count badge: ~11 DIP dark circle with a white 9px numeral, anchored to the
// glyph's bottom-right corner. Callers pass agent_count == 0 to hide it.
inline constexpr float kBadgeRadius = 5.5f;

// The toolbar button icon: 16 DIP glyph centered in the 18 DIP image, with
// the running-background-agents count badge composited at the glyph's
// bottom-right when |agent_count| >= 1 (composited into the image because
// Button::OnPaint is final at this pin).
gfx::ImageSkia CreateToolbarButtonIcon(SkColor brick_color, int agent_count);

}  // namespace prism

#endif  // CHROME_BROWSER_UI_VIEWS_PRISM_PRISM_BRAND_MARK_H_
