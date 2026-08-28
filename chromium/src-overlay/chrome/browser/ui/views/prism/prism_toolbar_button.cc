// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "ui/base/metadata/metadata_impl_macros.h"
#include "chrome/browser/ui/views/prism/prism_toolbar_button.h"

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/views/prism/prism_agent_menu.h"
#include "chrome/browser/ui/views/prism/prism_brand_mark.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/image/image_skia.h"

namespace prism {

PrismToolbarButton::PrismToolbarButton(Browser* browser)
    : ToolbarButton(base::BindRepeating(
          // The anchor is the button itself; the menu is created lazily so
          // its owner is fully constructed before first use.
          [](PrismToolbarButton* self, const ui::Event&) {
            if (!self->menu_) {
              self->menu_ = std::make_unique<PrismAgentMenu>();
            }
            self->menu_->Show(self->browser_, self);
          },
          this)),
      browser_(browser) {
  SetAccessibleName(u"Prism");
  SetTooltipText(u"Prism — agent activity");
}

PrismToolbarButton::~PrismToolbarButton() = default;

void PrismToolbarButton::OnThemeChanged() {
  ToolbarButton::OnThemeChanged();
  RefreshIcon();
}

void PrismToolbarButton::RefreshIcon() {
  const auto* provider = GetColorProvider();
  if (!provider) {
    return;
  }
  const SkColor ink = provider->GetColor(kColorToolbarButtonIcon);
  // 16 DIP glyph in an 18 DIP image: with the standard TOOLBAR_BUTTON insets
  // (7 DIP) the hit target lands at 32x32 DIP. No badge here (recon §7).
  SetImageModel(ButtonState::STATE_NORMAL,
                ui::ImageModel::FromImageSkia(CreateToolbarButtonIcon(ink)));
}

BEGIN_METADATA(PrismToolbarButton)
END_METADATA

}  // namespace prism
