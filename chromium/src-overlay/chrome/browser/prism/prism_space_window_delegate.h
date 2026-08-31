// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_PRISM_PRISM_SPACE_WINDOW_DELEGATE_H_
#define CHROME_BROWSER_PRISM_PRISM_SPACE_WINDOW_DELEGATE_H_

#include <map>
#include <optional>
#include <set>

#include "base/functional/callback.h"
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
  // Same, for callers that only hold a BrowserWindowInterface (e.g. the
  // tab-strip corner trigger, recon §7).
  void OpenSpacesOverview(BrowserWindowInterface* browser);
  // The space whose window currently hosts `wc` in its tab strip, or 0 when
  // it lives in the implicit default space's window (or no tracked window).
  // Used by the dashboard to know which card is "current" (recon §7).
  int SpaceIdForWebContents(content::WebContents* wc);
  // Inverse of the windows_ map: the space tracked to `window`, or 0 for the
  // implicit default space (window hosts no task space).
  int SpaceIdForWindow(const BrowserWindowInterface* window) const;
  // The window hosting the implicit default space: the first normal browser
  // window not tracked to a task space (recon §8 — the default browsing
  // context is a first-class wall card).
  BrowserWindowInterface* DefaultSpaceWindow() const;
  // The default space window's active tab (nullptr when there is none) —
  // the default wall card's thumbnail source.
  content::WebContents* ActiveTabForDefaultSpace() const;
  // Raises the window hosting `space_id` (Show + Activate). No-op when the
  // space has no tracked window. Used by the toolbar/omnibox agent menu.
  void FocusSpaceWindow(int space_id);
  // Cycles focus through [the implicit default space = the user's main
  // browsing area] + the spaces by id, raising the matching window. The
  // default space maps to the first window that hosts no space.
  void CycleToNextSpace();

  // ---- Spaces window mode (recon §8): the dashboard as a window-level mode
  // instead of a tab. BrowserView owns the mode state + the wall WebContents
  // (views layer); the delegate keeps a registry keyed by the wall
  // WebContents so the WebUI can ask for its "current" space and request the
  // mode exit without including BrowserView (layering).
  using SpacesModeExitCallback = base::RepeatingCallback<void()>;

  void RegisterSpacesMode(BrowserWindowInterface* window,
                          content::WebContents* wall_wc,
                          SpacesModeExitCallback exit_cb);
  void UnregisterSpacesMode(content::WebContents* wall_wc);
  bool IsSpacesModeWebContents(const content::WebContents* wc) const;
  // When the mode was last shown for `wc` (drives the wall's enter replay;
  // null Time when `wc` is not a mode wall).
  base::Time SpacesModeShownAt(const content::WebContents* wc) const;
  // The space the mode's window was showing when the mode opened (0 =
  // default space), for the dashboard's "current" card.
  int SpaceIdForModeWebContents(content::WebContents* wc);
  // Restores normal chrome (via the registered callback) and, when
  // `open_space_id` is set, focuses + shows that space.
  void ExitSpacesMode(content::WebContents* wall_wc,
                      std::optional<int> open_space_id);

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

  struct SpacesModeEntry {
    raw_ptr<BrowserWindowInterface> window;
    SpacesModeExitCallback exit;
    base::Time shown_at;
  };

  std::map<int, raw_ptr<BrowserWindowInterface>> windows_;
  std::map<int, TrackedBanner> banners_;
  // Active spaces-mode presentations, keyed by the wall WebContents.
  std::map<raw_ptr<content::WebContents>, SpacesModeEntry> spaces_modes_;
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
