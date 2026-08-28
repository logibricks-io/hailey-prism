// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/ui/views/prism/prism_agent_menu.h"

#include <string>

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/prism/prism_space_window_delegate.h"
#include "chrome/browser/ui/browser.h"
#include "content/public/browser/navigation_controller.h"
#include "prism/browser/spaces/space_manager.h"
#include "ui/base/mojom/menu_source_type.mojom-forward.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/widget/widget.h"

namespace prism {

namespace {

constexpr int kCommandOpenOverview = 1;
constexpr int kCommandOpenWelcome = 2;
// Space items carry their space id as the command id offset (>= 100).
constexpr int kCommandSpaceBase = 100;

class AgentMenuModel : public ui::SimpleMenuModel,
                       public ui::SimpleMenuModel::Delegate {
 public:
  explicit AgentMenuModel(Browser* browser)
      : ui::SimpleMenuModel(this), browser_(browser) {
    auto* manager = SpaceManager::GetInstance();
    bool has_spaces = false;
    for (const auto& space : manager->List()) {
      if (space.ownership != SpaceManager::Ownership::kAgent) {
        continue;
      }
      std::string label = space.name.empty() ? "Space" : space.name;
      if (!space.agent_task_state.empty()) {
        label += " — " + space.agent_task_state;
      }
      AddItem(kCommandSpaceBase + space.id, base::UTF8ToUTF16(label));
      has_spaces = true;
    }
    if (has_spaces) {
      AddSeparator(ui::NORMAL_SEPARATOR);
    }
    AddItem(kCommandOpenOverview, u"Open Spaces overview (⌥S)");
    AddItem(kCommandOpenWelcome, u"Open Prism welcome");
  }

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdEnabled(int command_id) const override { return true; }
  void ExecuteCommand(int command_id, int event_flags) override {
    if (command_id == kCommandOpenOverview) {
      GetPrismSpaceWindowDelegate()->OpenSpacesOverview(browser_);
      return;
    }
    if (command_id == kCommandOpenWelcome) {
      content::OpenURLParams params(GURL("chrome://prism-welcome"),
                                    content::Referrer(),
                                    WindowOpenDisposition::NEW_FOREGROUND_TAB,
                                    ui::PAGE_TRANSITION_AUTO_BOOKMARK, false);
      browser_->OpenURL(params, /*navigation_handle_callback=*/{});
      return;
    }
    GetPrismSpaceWindowDelegate()->FocusSpaceWindow(command_id -
                                                    kCommandSpaceBase);
  }

 private:
  raw_ptr<Browser> browser_;
};

}  // namespace

PrismAgentMenu::PrismAgentMenu() = default;
PrismAgentMenu::~PrismAgentMenu() = default;

void PrismAgentMenu::Show(Browser* browser, views::View* anchor) {
  if (!browser || !anchor || !anchor->GetWidget()) {
    return;
  }
  model_ = std::make_unique<AgentMenuModel>(browser);
  runner_ = std::make_unique<views::MenuRunner>(
      model_.get(), views::MenuRunner::HAS_MNEMONICS);
  // RunMenuAt is a blocking call for native menus on macOS — runner_ and
  // model_ stay owned by this instance for the menu's lifetime.
  runner_->RunMenuAt(anchor->GetWidget(), /*menu_button_controller=*/nullptr,
                     anchor->GetAnchorBoundsInScreen(),
                     views::MenuAnchorPosition::kTopLeft,
                     ui::mojom::MenuSourceType::kNone);
}

}  // namespace prism
