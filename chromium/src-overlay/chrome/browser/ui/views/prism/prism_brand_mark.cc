// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/ui/views/prism/prism_brand_mark.h"

#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/image/image_skia_rep.h"

#include <string>

#include "base/strings/utf_string_conversions.h"

namespace prism {

void PaintLogiBricksMark(gfx::Canvas* canvas,
                         const gfx::RectF& bounds,
                         SkColor brick_color) {
  const float side = std::min(bounds.width(), bounds.height());
  const float ox = bounds.x() + (bounds.width() - side) / 2.f;
  const float oy = bounds.y() + (bounds.height() - side) / 2.f;
  const float s = side / 576.f;

  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(brick_color);
  constexpr float kBricks[5][2] = {
      {0, 0}, {0, 209}, {0, 418}, {209, 418}, {418, 418}};
  for (const auto& cell : kBricks) {
    canvas->DrawRoundRect(
        gfx::RectF(ox + cell[0] * s, oy + cell[1] * s, 158 * s, 158 * s),
        21 * s, flags);
  }
  flags.setColor(SkColorSetRGB(0xC8, 0x78, 0x58));
  canvas->DrawCircle(gfx::PointF(ox + 497 * s, oy + 79 * s), 71 * s, flags);
}

gfx::ImageSkia CreateLogiBricksMarkImage(int size_px, SkColor brick_color) {
  gfx::ImageSkia image;
  for (float scale : {1.0f, 2.0f, 3.0f}) {
    gfx::Canvas canvas(gfx::Size(size_px, size_px), scale,
                       /*is_opaque=*/false);
    PaintLogiBricksMark(&canvas, gfx::RectF(size_px, size_px), brick_color);
    image.AddRepresentation(
        gfx::ImageSkiaRep(canvas.GetBitmap(), scale));
  }
  return image;
}

gfx::ImageSkia CreateToolbarButtonIcon(SkColor brick_color, int agent_count) {
  gfx::ImageSkia image;
  for (float scale : {1.0f, 2.0f, 3.0f}) {
    gfx::Canvas canvas(gfx::Size(kToolbarButtonImageSize,
                                 kToolbarButtonImageSize),
                       scale, /*is_opaque=*/false);
    const float glyph_origin =
        (kToolbarButtonImageSize - kToolbarButtonGlyphSize) / 2.f;
    PaintLogiBricksMark(&canvas,
                        gfx::RectF(glyph_origin, glyph_origin,
                                   kToolbarButtonGlyphSize,
                                   kToolbarButtonGlyphSize),
                        brick_color);
    if (agent_count >= 1) {
      // Center one radius in from the image's bottom-right corner so the
      // circle straddles the glyph's bottom-right corner without clipping.
      const gfx::PointF center(kToolbarButtonImageSize - kBadgeRadius,
                               kToolbarButtonImageSize - kBadgeRadius);
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setColor(SkColorSetRGB(0x1f, 0x1f, 0x1f));
      canvas.DrawCircle(center, kBadgeRadius, flags);
      const std::u16string numeral =
          base::UTF8ToUTF16(std::to_string(agent_count));
      const int text_h = static_cast<int>(kBadgeRadius * 2);
      canvas.DrawStringRectWithFlags(
          numeral,
          gfx::FontList().DeriveWithSizeDelta(-3),  // 12px -> 9px
          SK_ColorWHITE,
          gfx::Rect(static_cast<int>(center.x() - kBadgeRadius),
                    static_cast<int>(center.y() - text_h / 2.f),
                    static_cast<int>(kBadgeRadius * 2), text_h),
          gfx::Canvas::TEXT_ALIGN_CENTER);
    }
    image.AddRepresentation(
        gfx::ImageSkiaRep(canvas.GetBitmap(), scale));
  }
  return image;
}

}  // namespace prism
