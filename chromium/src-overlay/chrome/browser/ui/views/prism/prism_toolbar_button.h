// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_VIEWS_PRISM_PRISM_TOOLBAR_BUTTON_H_
#define CHROME_BROWSER_UI_VIEWS_PRISM_PRISM_TOOLBAR_BUTTON_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/views/toolbar/toolbar_button.h"

class Browser;

namespace prism {

class PrismAgentMenu;

// Top-right Prism button (recon §5): LogiBricks mark with a dark circular
// badge counting agent-controlled spaces (hidden at 0, same source as the
// Dock badge's 1s sync). Click opens the agent menu.
class PrismToolbarButton : public ToolbarButton {
  METADATA_HEADER(PrismToolbarButton, ToolbarButton)

 public:
  explicit PrismToolbarButton(Browser* browser);
  ~PrismToolbarButton() override;

  // ToolbarButton:
  void OnThemeChanged() override;

 private:
  void RefreshIcon();
  void SyncBadge();

  raw_ptr<Browser> browser_;
  int agent_count_ = 0;
  base::RepeatingTimer badge_timer_;
  std::unique_ptr<PrismAgentMenu> menu_;
};

}  // namespace prism

#endif  // CHROME_BROWSER_UI_VIEWS_PRISM_PRISM_TOOLBAR_BUTTON_H_
