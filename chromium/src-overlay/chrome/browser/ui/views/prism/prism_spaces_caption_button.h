// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_VIEWS_PRISM_PRISM_SPACES_CAPTION_BUTTON_H_
#define CHROME_BROWSER_UI_VIEWS_PRISM_PRISM_SPACES_CAPTION_BUTTON_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/timer/timer.h"
#include "ui/views/controls/button/label_button.h"

class BrowserWindowInterface;

namespace ui {
class MenuModel;
}

namespace views {
class MenuRunner;
}

namespace prism {

// The spaces mode's native top-center caption (recon §8): "N Spaces ⌄" at
// tab-strip height, replacing the in-page caption while the window-level
// dashboard is active. The dropdown lists the spaces; choosing one exits
// the mode into that space. Visible only in spaces mode
// (HorizontalTabStripRegionView toggles it).
class PrismSpacesCaptionButton : public views::LabelButton {
  METADATA_HEADER(PrismSpacesCaptionButton, views::LabelButton)

 public:
  explicit PrismSpacesCaptionButton(
      BrowserWindowInterface* browser_window_interface);
  ~PrismSpacesCaptionButton() override;

  // views::LabelButton:
  void OnThemeChanged() override;

 private:
  void RefreshText();
  void ShowMenu();

  raw_ptr<BrowserWindowInterface> browser_window_interface_;
  base::RepeatingTimer text_timer_;
  std::unique_ptr<ui::MenuModel> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
};

}  // namespace prism

#endif  // CHROME_BROWSER_UI_VIEWS_PRISM_PRISM_SPACES_CAPTION_BUTTON_H_
