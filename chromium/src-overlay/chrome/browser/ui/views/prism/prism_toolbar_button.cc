// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "ui/base/metadata/metadata_impl_macros.h"
#include "chrome/browser/ui/views/prism/prism_toolbar_button.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/views/prism/prism_agent_menu.h"
#include "chrome/browser/ui/views/prism/prism_brand_mark.h"
#include "prism/browser/spaces/space_manager.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/point_f.h"
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
  // Same 1s cadence as the Dock badge's SyncAgentSurfaces.
  SyncBadge();
  badge_timer_.Start(
      FROM_HERE, base::Seconds(1),
      base::BindRepeating(&PrismToolbarButton::SyncBadge,
                          base::Unretained(this)));
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
  // The badge is composited into the icon (Button::OnPaint is final): it
  // tracks the icon across themes and sizes. Hidden entirely at 0 agents.
  SetImageModel(ButtonState::STATE_NORMAL,
                ui::ImageModel::FromImageSkia(
                    agent_count_ > 0
                        ? CreateLogiBricksMarkImageWithBadge(20, ink,
                                                             agent_count_)
                        : CreateLogiBricksMarkImage(20, ink)));
}

void PrismToolbarButton::SyncBadge() {
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
  RefreshIcon();
}

BEGIN_METADATA(PrismToolbarButton)
END_METADATA

}  // namespace prism
