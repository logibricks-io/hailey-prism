// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "ui/base/metadata/metadata_impl_macros.h"
#include "chrome/browser/ui/views/prism/prism_brand_chip_view.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/views/prism/prism_agent_menu.h"
#include "chrome/browser/ui/views/prism/prism_brand_mark.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/views/border.h"

namespace prism {

PrismBrandChipView::PrismBrandChipView(Browser* browser)
    : views::LabelButton(
          base::BindRepeating(
              // The anchor is the chip itself; the menu is created lazily so
              // its owner is fully constructed before first use.
              [](PrismBrandChipView* self, const ui::Event&) {
                if (!self->menu_) {
                  self->menu_ = std::make_unique<PrismAgentMenu>();
                }
                self->menu_->Show(self->browser_, self);
              },
              this),
          u"Prism"),
      browser_(browser) {
  SetTooltipText(u"Prism — agent menu");
  SetAccessibleName(u"Prism");
  SetImageLabelSpacing(4);
  // ego's chip measures 85x22: compact pill, 10px horizontal padding.
  SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 10)));
}

PrismBrandChipView::~PrismBrandChipView() = default;

void PrismBrandChipView::OnThemeChanged() {
  views::LabelButton::OnThemeChanged();
  const auto* provider = GetColorProvider();
  if (!provider) {
    return;
  }
  const SkColor text = provider->GetColor(kColorOmniboxText);
  SetEnabledTextColors(text);
  SetImageModel(ButtonState::STATE_NORMAL,
                ui::ImageModel::FromImageSkia(
                    CreateLogiBricksMarkImage(14, text)));
}

void PrismBrandChipView::OnPaintBackground(gfx::Canvas* canvas) {
  // Replace the default background with a subtle pill (ego's chip style).
  const auto* provider = GetColorProvider();
  const SkColor ink = provider ? provider->GetColor(kColorOmniboxText)
                               : SK_ColorWHITE;
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(SkColorSetA(ink, 0x14));
  canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), height() / 2.f, flags);
}

BEGIN_METADATA(PrismBrandChipView)
END_METADATA

}  // namespace prism
