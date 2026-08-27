// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_PRISM_PRISM_DOCK_BADGE_H_
#define CHROME_BROWSER_PRISM_PRISM_DOCK_BADGE_H_

namespace prism {

// macOS Dock tile badge showing how many spaces an agent currently controls
// (the pragmatic stand-in for ego's toolbar activity badge). 0 clears the
// badge. Must be called on the UI (main) thread.
void SetDockBadgeCount(int agent_controlled_spaces);

}  // namespace prism

#endif  // CHROME_BROWSER_PRISM_PRISM_DOCK_BADGE_H_
