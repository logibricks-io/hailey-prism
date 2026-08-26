// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef PRISM_BROWSER_SPACES_SPACE_WINDOW_DELEGATE_H_
#define PRISM_BROWSER_SPACES_SPACE_WINDOW_DELEGATE_H_

#include <memory>
#include <vector>

namespace content {
class WebContents;
}  // namespace content

namespace prism {

// Chrome-layer services for Space windows (Phase 4). Implemented and injected
// by chrome/ at startup (see chrome/browser/prism/), called from the content
// layer's PrismDomainHandler. Kept free of chrome types so //prism:prism
// stays content-public-clean.
//
// Layering decision (option B): the Prism.* domain handler stays registered
// in content/ (patch 0001); chrome features are reached through this delegate
// instead of moving handler registration into chrome/. Rationale: the
// registration site, the DevToolsSession policy hooks and the existing probes
// all stay untouched; the only new surface is this interface.
class SpaceWindowDelegate {
 public:
  virtual ~SpaceWindowDelegate() = default;

  // Opens (or focuses) a visible browser window that hosts the space's tabs.
  // `windowless_tabs` are the space's current windowless WebContents;
  // ownership transfers into the window's tab strip.
  virtual void ShowTaskSpace(
      int space_id,
      std::vector<std::unique_ptr<content::WebContents>> windowless_tabs) = 0;

  // Brief click-highlight marker at (x, y) — page coordinates of the space
  // window's active tab; no-op when the space is not shown.
  virtual void AnimateClickHighlight(int space_id, int x, int y) = 0;
};

// Global injection point (browser process, UI thread).
SpaceWindowDelegate* GetSpaceWindowDelegate();
void SetSpaceWindowDelegate(SpaceWindowDelegate* delegate);

}  // namespace prism

#endif  // PRISM_BROWSER_SPACES_SPACE_WINDOW_DELEGATE_H_
