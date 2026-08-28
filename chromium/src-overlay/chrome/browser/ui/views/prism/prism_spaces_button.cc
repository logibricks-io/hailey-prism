// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "ui/base/metadata/metadata_impl_macros.h"
#include "chrome/browser/ui/views/prism/prism_spaces_button.h"

#include <string>

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/prism/prism_space_window_delegate.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/vector_icons/vector_icons.h"
#include "prism/browser/spaces/space_manager.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_rep.h"
#include "ui/views/accessibility/view_accessibility.h"

namespace prism {

namespace {

// ego's trigger measures 30x30 DIP (recon §7).
constexpr int kTriggerButtonSize = 30;
// The count state renders as an 18 DIP dark circle centered in the button.
constexpr float kCountCircleRadius = 9.f;

// Four-squares grid, drawn at 16 DIP (TabStripControlButton::kIconSize).
gfx::ImageSkia CreateGridIcon(SkColor color) {
  gfx::ImageSkia image;
  for (float scale : {1.0f, 2.0f, 3.0f}) {
    gfx::Canvas canvas(gfx::Size(16, 16), scale, /*is_opaque=*/false);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(color);
    constexpr float kCell = 6.5f;
    constexpr float kPitch = 9.5f;
    for (const auto& cell :
         {gfx::PointF(0, 0), gfx::PointF(kPitch, 0), gfx::PointF(0, kPitch),
          gfx::PointF(kPitch, kPitch)}) {
      canvas.DrawRoundRect(gfx::RectF(cell.x(), cell.y(), kCell, kCell), 1.8f,
                           flags);
    }
    image.AddRepresentation(gfx::ImageSkiaRep(canvas.GetBitmap(), scale));
  }
  return image;
}

// Dark circle + white numeral (24 DIP canvas so the circle reads as a badge
// inside the 30 DIP button). A hairline ring keeps the dark circle legible on
// dark themes.
gfx::ImageSkia CreateCountIcon(int count) {
  gfx::ImageSkia image;
  for (float scale : {1.0f, 2.0f, 3.0f}) {
    gfx::Canvas canvas(gfx::Size(24, 24), scale, /*is_opaque=*/false);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(SkColorSetRGB(0x1f, 0x1f, 0x1f));
    const gfx::PointF center(12.f, 12.f);
    canvas.DrawCircle(center, kCountCircleRadius, flags);
    flags.setColor(SkColorSetA(SK_ColorWHITE, 0x26));
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1.f);
    canvas.DrawCircle(center, kCountCircleRadius - 0.5f, flags);
    const std::u16string numeral = base::UTF8ToUTF16(std::to_string(count));
    canvas.DrawStringRectWithFlags(
        numeral,
        gfx::FontList().DeriveWithSizeDelta(-3),  // 12px -> 9px
        SK_ColorWHITE,
        gfx::Rect(static_cast<int>(center.x() - kCountCircleRadius),
                  static_cast<int>(center.y() - kCountCircleRadius),
                  static_cast<int>(kCountCircleRadius * 2),
                  static_cast<int>(kCountCircleRadius * 2)),
        gfx::Canvas::TEXT_ALIGN_CENTER);
    image.AddRepresentation(gfx::ImageSkiaRep(canvas.GetBitmap(), scale));
  }
  return image;
}

}  // namespace

PrismSpacesButton::PrismSpacesButton(
    BrowserWindowInterface* browser_window_interface)
    : TabStripControlButton(
          browser_window_interface,
          base::BindRepeating(
              // recon §8: the trigger toggles the window-level spaces mode
              // (same as ⌥S) instead of opening the overview in a tab.
              [](BrowserWindowInterface* bwi, const ui::Event&) {
                if (auto* browser_view =
                        BrowserView::GetBrowserViewForNativeWindow(
                            bwi->GetWindow()->GetNativeWindow())) {
                  browser_view->SetSpacesModeActive(
                      !browser_view->spaces_mode_active(), std::nullopt);
                }
              },
              browser_window_interface),
          // The stored vector icon is never painted: UpdateIcon() below draws
          // both trigger states. A valid static reference is required.
          vector_icons::kSelectWindowIcon,
          Edge::kNone,
          Edge::kNone) {
  SetTooltipText(u"Open Space (⌥S)");
  GetViewAccessibility().SetName(u"Open Space (⌥S)");
  SetPreferredSize(gfx::Size(kTriggerButtonSize, kTriggerButtonSize));
  // Same 1s cadence as the Dock badge's SyncAgentSurfaces.
  SyncCount();
  count_timer_.Start(FROM_HERE, base::Seconds(1),
                     base::BindRepeating(&PrismSpacesButton::SyncCount,
                                         base::Unretained(this)));
}

PrismSpacesButton::~PrismSpacesButton() = default;

void PrismSpacesButton::UpdateIcon() {
  ui::ImageModel model;
  if (agent_count_ > 0) {
    model = ui::ImageModel::FromImageSkia(CreateCountIcon(agent_count_));
  } else {
    const auto* provider = GetColorProvider();
    const SkColor ink =
        provider ? provider->GetColor(GetForegroundColor()) : SK_ColorWHITE;
    model = ui::ImageModel::FromImageSkia(CreateGridIcon(ink));
  }
  SetImageModel(views::Button::STATE_NORMAL, model);
  SetImageModel(views::Button::STATE_HOVERED, model);
  SetImageModel(views::Button::STATE_PRESSED, model);
}

void PrismSpacesButton::SyncCount() {
  int count = 0;
  for (const auto& space : SpaceManager::GetInstance()->List()) {
    if (space.ownership == SpaceManager::Ownership::kAgent) {
      ++count;
    }
  }
  if (count == agent_count_) {
    return;
  }
  agent_count_ = count;
  UpdateIcon();
}

BEGIN_METADATA(PrismSpacesButton)
END_METADATA

}  // namespace prism
