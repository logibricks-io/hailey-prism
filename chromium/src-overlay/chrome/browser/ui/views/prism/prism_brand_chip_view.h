// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_VIEWS_PRISM_PRISM_BRAND_CHIP_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_PRISM_PRISM_BRAND_CHIP_VIEW_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "ui/views/controls/button/label_button.h"

class Browser;

namespace prism {

class PrismAgentMenu;

// Omnibox leading chip (recon §5): LogiBricks mark + "Prism" text in a pill
// at the left edge of the location bar. Click opens the agent menu.
class PrismBrandChipView : public views::LabelButton {
  METADATA_HEADER(PrismBrandChipView, views::LabelButton)

 public:
  explicit PrismBrandChipView(Browser* browser);
  ~PrismBrandChipView() override;

  // views::LabelButton:
  void OnThemeChanged() override;
  void OnPaintBackground(gfx::Canvas* canvas) override;

 private:
  raw_ptr<Browser> browser_;
  std::unique_ptr<PrismAgentMenu> menu_;
};

}  // namespace prism

#endif  // CHROME_BROWSER_UI_VIEWS_PRISM_PRISM_BRAND_CHIP_VIEW_H_
