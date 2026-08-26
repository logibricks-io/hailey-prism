// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "prism/browser/devtools/prism_domain_handler.h"

namespace prism {

const char* WireCodeForSpaceError(SpaceManager::Error error) {
  switch (error) {
    case SpaceManager::Error::kNone:
      return "";
    case SpaceManager::Error::kNotFound:
      return kErrNotFound;
    case SpaceManager::Error::kUserInControl:
      return kErrUserInControl;
  }
  return kErrUnavailable;
}

}  // namespace prism
