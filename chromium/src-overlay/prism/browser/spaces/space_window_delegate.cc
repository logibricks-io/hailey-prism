// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "prism/browser/spaces/space_window_delegate.h"

namespace prism {

namespace {
SpaceWindowDelegate* g_delegate = nullptr;
}  // namespace

SpaceWindowDelegate* GetSpaceWindowDelegate() {
  return g_delegate;
}

void SetSpaceWindowDelegate(SpaceWindowDelegate* delegate) {
  g_delegate = delegate;
}

}  // namespace prism
