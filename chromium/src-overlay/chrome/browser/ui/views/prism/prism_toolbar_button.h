// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_VIEWS_PRISM_PRISM_TOOLBAR_BUTTON_H_
#define CHROME_BROWSER_UI_VIEWS_PRISM_PRISM_TOOLBAR_BUTTON_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/views/toolbar/toolbar_button.h"

class Browser;

namespace prism {

class PrismAgentMenu;

// Top-right Prism button (recon §5): the LogiBricks mark opening the agent
// menu. Per recon §7 this toolbar-row button carries NO badge — the
// running-agents count lives on the tab-strip corner trigger
// (PrismSpacesButton).
class PrismToolbarButton : public ToolbarButton {
  METADATA_HEADER(PrismToolbarButton, ToolbarButton)

 public:
  explicit PrismToolbarButton(Browser* browser);
  ~PrismToolbarButton() override;

  // ToolbarButton:
  void OnThemeChanged() override;

 private:
  void RefreshIcon();

  raw_ptr<Browser> browser_;
  std::unique_ptr<PrismAgentMenu> menu_;
};

}  // namespace prism

#endif  // CHROME_BROWSER_UI_VIEWS_PRISM_PRISM_TOOLBAR_BUTTON_H_
