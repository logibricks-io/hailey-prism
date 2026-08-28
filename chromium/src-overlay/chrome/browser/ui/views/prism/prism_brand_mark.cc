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

gfx::ImageSkia CreateLogiBricksMarkImageWithBadge(int size_px,
                                                  SkColor brick_color,
                                                  int count) {
  gfx::ImageSkia image;
  for (float scale : {1.0f, 2.0f}) {
    gfx::Canvas canvas(gfx::Size(size_px, size_px), scale,
                       /*is_opaque=*/false);
    PaintLogiBricksMark(&canvas, gfx::RectF(size_px, size_px), brick_color);
    const float r = size_px * 0.33f;
    const gfx::PointF center(size_px - r * 0.9f, size_px - r * 0.9f);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(SkColorSetRGB(0x1f, 0x1f, 0x1f));
    canvas.DrawCircle(center, r, flags);
    flags.setColor(SK_ColorWHITE);
    const std::u16string numeral =
        base::UTF8ToUTF16(std::to_string(count));
    const int text_h = static_cast<int>(r * 1.25f);
    canvas.DrawStringRectWithFlags(
        numeral,
        gfx::FontList().DeriveWithSizeDelta(
            static_cast<int>(-4 * scale)),
        SK_ColorWHITE,
        gfx::Rect(static_cast<int>(center.x() - r),
                  static_cast<int>(center.y() - text_h / 2.f),
                  static_cast<int>(r * 2), text_h),
        gfx::Canvas::TEXT_ALIGN_CENTER);
    image.AddRepresentation(
        gfx::ImageSkiaRep(canvas.GetBitmap(), scale));
  }
  return image;
}

gfx::ImageSkia CreateLogiBricksMarkImage(int size_px, SkColor brick_color) {
  gfx::ImageSkia image;
  for (float scale : {1.0f, 2.0f}) {
    gfx::Canvas canvas(gfx::Size(size_px, size_px), scale,
                       /*is_opaque=*/false);
    PaintLogiBricksMark(&canvas, gfx::RectF(size_px, size_px), brick_color);
    image.AddRepresentation(
        gfx::ImageSkiaRep(canvas.GetBitmap(), scale));
  }
  return image;
}

}  // namespace prism
