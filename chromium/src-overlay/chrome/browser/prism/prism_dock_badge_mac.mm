// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#import "chrome/browser/prism/prism_dock_badge.h"

#import <Cocoa/Cocoa.h>

namespace prism {

void SetDockBadgeCount(int agent_controlled_spaces) {
  NSString* label = agent_controlled_spaces > 0
                        ? [NSString stringWithFormat:@"%d",
                                                     agent_controlled_spaces]
                        : nil;
  [[NSApp dockTile] setBadgeLabel:label];
}

}  // namespace prism
