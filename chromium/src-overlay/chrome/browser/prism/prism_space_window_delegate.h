// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_PRISM_PRISM_SPACE_WINDOW_DELEGATE_H_
#define CHROME_BROWSER_PRISM_PRISM_SPACE_WINDOW_DELEGATE_H_

#include <map>
#include <set>

#include "base/memory/raw_ptr.h"
#include "base/timer/timer.h"
#include "prism/browser/spaces/space_window_delegate.h"

class Browser;
class BrowserWindowInterface;

namespace content {
class WebContents;
}

namespace infobars {
class InfoBar;
}

namespace prism {

// Chrome-side implementation of SpaceWindowDelegate (Phase 4): opens and
// tracks one visible Browser window per space, hosts the space's windowless
// agent tabs in it, pins chrome://prism-spaces as the window's identity tab,
// and paints the agent click-highlight overlay.
//
// Phase 5 adds the user-facing surfaces: the ⌥S/View-menu space switching
// entry points, an "Agent is in control" infobar on space windows, and the
// Dock badge counting agent-controlled spaces.
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
  bool AppendTabToSpaceWindow(int space_id,
                              std::unique_ptr<content::WebContents> tab)
      override;

  // Phase 5: user-facing space switching (View menu items / keyboard).
  // Opens chrome://prism-spaces in a new foreground tab of `browser`.
  void OpenSpacesOverview(Browser* browser);
  // Raises the window hosting `space_id` (Show + Activate). No-op when the
  // space has no tracked window. Used by the toolbar/omnibox agent menu.
  void FocusSpaceWindow(int space_id);
  // Cycles focus through [the implicit default space = the user's main
  // browsing area] + the spaces by id, raising the matching window. The
  // default space maps to the first window that hosts no space.
  void CycleToNextSpace();

 private:
  // Validated lookup: the tracked window, or nullptr when it was closed.
  BrowserWindowInterface* FindSpaceWindow(int space_id);

  // 1s poll reconciling the agent-in-control banner on every space window
  // and the Dock badge with the SpaceManager ownership state (handoffs can
  // arrive over CDP without the chrome layer noticing).
  void SyncAgentSurfaces();
  void OnBannerDismissed(int space_id);

  struct TrackedBanner {
    raw_ptr<content::WebContents> web_contents;
    raw_ptr<infobars::InfoBar> infobar;
  };

  std::map<int, raw_ptr<BrowserWindowInterface>> windows_;
  std::map<int, TrackedBanner> banners_;
  // Spaces whose banner the user closed with the X; suppressed until the
  // ownership leaves kAgent (then re-enters).
  std::set<int> dismissed_banners_;
  base::RepeatingTimer agent_surface_timer_;
};

// Process-wide accessor. The delegate is registered into the prism
// injection point from RegisterChromeWebUIConfigs (a guaranteed-early chrome
// startup call — a static initializer would be dead-stripped by the linker).
PrismSpaceWindowDelegate* GetPrismSpaceWindowDelegate();

}  // namespace prism

#endif  // CHROME_BROWSER_PRISM_PRISM_SPACE_WINDOW_DELEGATE_H_
