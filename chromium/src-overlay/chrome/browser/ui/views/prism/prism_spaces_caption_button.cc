// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "ui/base/metadata/metadata_impl_macros.h"
#include "chrome/browser/ui/views/prism/prism_spaces_caption_button.h"

#include <string>

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "prism/browser/spaces/space_manager.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/base/mojom/menu_source_type.mojom-forward.h"
#include "ui/color/color_provider.h"
#include "ui/views/controls/menu/menu_runner.h"

namespace prism {

namespace {

class CaptionMenuModel : public ui::SimpleMenuModel,
                         public ui::SimpleMenuModel::Delegate {
 public:
  explicit CaptionMenuModel(BrowserWindowInterface* bwi)
      : ui::SimpleMenuModel(this), bwi_(bwi) {
    Build();
  }

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override {
    if (auto* browser_view = BrowserView::GetBrowserViewForNativeWindow(
            bwi_->GetWindow()->GetNativeWindow())) {
      browser_view->SetSpacesModeActive(false, command_id);
    }
  }

 private:
  void Build() {
    int count = 0;
    for (const auto& space : SpaceManager::GetInstance()->List()) {
      AddItem(space.id, base::UTF8ToUTF16(space.name));
      ++count;
    }
    if (count == 0) {
      AddItem(0, u"Default space");
    }
  }

  raw_ptr<BrowserWindowInterface> bwi_;
};

}  // namespace

PrismSpacesCaptionButton::PrismSpacesCaptionButton(
    BrowserWindowInterface* browser_window_interface)
    : views::LabelButton(base::BindRepeating(
                             [](PrismSpacesCaptionButton* self,
                                const ui::Event&) { self->ShowMenu(); },
                             this),
                         u"Spaces"),
      browser_window_interface_(browser_window_interface) {
  SetAccessibleName(u"Spaces");
  SetTooltipText(u"Spaces");
  SetHorizontalAlignment(gfx::ALIGN_CENTER);
  RefreshText();
  text_timer_.Start(FROM_HERE, base::Seconds(1),
                    base::BindRepeating(&PrismSpacesCaptionButton::RefreshText,
                                        base::Unretained(this)));
}

PrismSpacesCaptionButton::~PrismSpacesCaptionButton() = default;

void PrismSpacesCaptionButton::OnThemeChanged() {
  views::LabelButton::OnThemeChanged();
  const auto* provider = GetColorProvider();
  if (provider) {
    SetEnabledTextColors(provider->GetColor(kColorTabForegroundActiveFrameActive));
  }
}

void PrismSpacesCaptionButton::RefreshText() {
  const size_t count = SpaceManager::GetInstance()->List().size();
  SetText(base::UTF8ToUTF16(std::to_string(count) + " Space" +
                            (count == 1 ? "" : "s") + " ⌄"));
}

void PrismSpacesCaptionButton::ShowMenu() {
  menu_model_ = std::make_unique<CaptionMenuModel>(browser_window_interface_);
  menu_runner_ = std::make_unique<views::MenuRunner>(
      menu_model_.get(), views::MenuRunner::HAS_MNEMONICS);
  // RunMenuAt is blocking on macOS; the model + runner stay alive as members
  // (same ownership pattern as PrismAgentMenu).
  menu_runner_->RunMenuAt(GetWidget(), nullptr, GetAnchorBoundsInScreen(),
                          views::MenuAnchorPosition::kTopLeft,
                          ui::mojom::MenuSourceType::kNone);
  menu_runner_.reset();
  menu_model_.reset();
}

BEGIN_METADATA(PrismSpacesCaptionButton)
END_METADATA

}  // namespace prism
