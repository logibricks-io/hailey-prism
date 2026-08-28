// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_VIEWS_PRISM_PRISM_SPACES_BUTTON_H_
#define CHROME_BROWSER_UI_VIEWS_PRISM_PRISM_SPACES_BUTTON_H_

#include "base/timer/timer.h"
#include "chrome/browser/ui/views/tabs/tab_strip_control_button.h"

namespace prism {

// Spaces dashboard trigger on the TAB STRIP row, pinned to the window's
// top-right corner (recon §7; ego measures 30x30, "Open Space (⌥S)").
// With 0 agents running it shows a four-squares grid icon; with agents it
// renders as a dark circle with a white numeral — the running-agents count.
// Click opens the spaces overview, the same action as IDC_PRISM_SPACES_OVERVIEW
// (⌥S). The count polls SpaceManager on the Dock badge's 1s cadence.
class PrismSpacesButton : public TabStripControlButton {
  METADATA_HEADER(PrismSpacesButton, TabStripControlButton)

 public:
  explicit PrismSpacesButton(BrowserWindowInterface* browser_window_interface);
  ~PrismSpacesButton() override;

  // TabStripControlButton:
  void UpdateIcon() override;

 private:
  void SyncCount();

  int agent_count_ = 0;
  base::RepeatingTimer count_timer_;
};

}  // namespace prism

#endif  // CHROME_BROWSER_UI_VIEWS_PRISM_PRISM_SPACES_BUTTON_H_
