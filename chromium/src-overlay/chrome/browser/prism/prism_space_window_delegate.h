// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_PRISM_PRISM_SPACE_WINDOW_DELEGATE_H_
#define CHROME_BROWSER_PRISM_PRISM_SPACE_WINDOW_DELEGATE_H_

#include <map>

#include "base/memory/raw_ptr.h"
#include "prism/browser/spaces/space_window_delegate.h"

class BrowserWindowInterface;

namespace prism {

// Chrome-side implementation of SpaceWindowDelegate (Phase 4): opens and
// tracks one visible Browser window per space, hosts the space's windowless
// agent tabs in it, pins chrome://prism-spaces as the window's identity tab,
// and paints the agent click-highlight overlay.
//
// Self-registers at dylib load (static initializer) — see the .cc.
class PrismSpaceWindowDelegate : public SpaceWindowDelegate {
 public:
  PrismSpaceWindowDelegate();
  ~PrismSpaceWindowDelegate() override;

  // SpaceWindowDelegate:
  void ShowTaskSpace(
      int space_id,
      std::vector<std::unique_ptr<content::WebContents>> windowless_tabs)
      override;
  void AnimateClickHighlight(int space_id, int x, int y) override;

 private:
  // Validated lookup: the tracked window, or nullptr when it was closed.
  BrowserWindowInterface* FindSpaceWindow(int space_id);

  std::map<int, raw_ptr<BrowserWindowInterface>> windows_;
};

// Process-wide accessor. The delegate is registered into the prism
// injection point from RegisterChromeWebUIConfigs (a guaranteed-early chrome
// startup call — a static initializer would be dead-stripped by the linker).
PrismSpaceWindowDelegate* GetPrismSpaceWindowDelegate();

}  // namespace prism

#endif  // CHROME_BROWSER_PRISM_PRISM_SPACE_WINDOW_DELEGATE_H_
