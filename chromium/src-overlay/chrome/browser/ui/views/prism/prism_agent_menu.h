// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_VIEWS_PRISM_PRISM_AGENT_MENU_H_
#define CHROME_BROWSER_UI_VIEWS_PRISM_PRISM_AGENT_MENU_H_

#include <memory>

#include "base/memory/raw_ptr.h"

class Browser;

namespace views {
class MenuRunner;
class View;
}

namespace ui {
class MenuModel;
}

namespace prism {

// The Prism agent menu (recon §5), opened from the omnibox brand chip or the
// toolbar button. Contents: one item per agent-controlled space (focuses its
// window), then "Open Spaces overview" and "Open Prism welcome".
//
// Owns the menu model + runner: RunMenuAt is a blocking call for native
// menus on macOS, so self-owning-on-close patterns are unsafe; callers keep
// an instance as a member (menu_).
class PrismAgentMenu {
 public:
  PrismAgentMenu();
  ~PrismAgentMenu();

  void Show(Browser* browser, views::View* anchor);

 private:
  std::unique_ptr<ui::MenuModel> model_;
  std::unique_ptr<views::MenuRunner> runner_;
};

}  // namespace prism

#endif  // CHROME_BROWSER_UI_VIEWS_PRISM_PRISM_AGENT_MENU_H_
