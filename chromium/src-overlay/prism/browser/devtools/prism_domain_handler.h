// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef PRISM_BROWSER_DEVTOOLS_PRISM_DOMAIN_HANDLER_H_
#define PRISM_BROWSER_DEVTOOLS_PRISM_DOMAIN_HANDLER_H_

#include <memory>
#include <optional>

#include "prism/browser/spaces/space_manager.h"

// TODO(after-fetch): verify the generated protocol interface names against
// the pinned tree. This handler subclasses the PDL-generated
// `content::protocol::Prism::Backend` (built from prism/pdl/prism.pdl) and is
// registered for browser-target sessions in devtools_session.cc (patch
// series). Generated method signatures vary slightly between milestones, so
// the .cc method bodies are written at build time, not here.
//
// Responsibilities:
//   - hold per-session state: the selected Space id (connection = state,
//     ADR-002);
//   - translate Prism.* commands into SpaceManager / window operations;
//   - map failures to the stable PRISM_* code prefix in the CDP error
//     message (binding-contract.md §3).

namespace content {
class WebContents;
}

namespace prism {

// Stable wire codes (binding-contract.md §3.2). Message prefix format:
// "<CODE>: <human readable detail>".
inline constexpr char kErrBrowserUnavailable[] = "PRISM_BROWSER_UNAVAILABLE";
inline constexpr char kErrNotFound[] = "PRISM_TASK_SPACE_NOT_FOUND";
inline constexpr char kErrNotSelected[] = "PRISM_TASK_SPACE_NOT_SELECTED";
inline constexpr char kErrInactive[] = "PRISM_TASK_SPACE_INACTIVE";
inline constexpr char kErrUserInControl[] = "PRISM_TASK_SPACE_USER_IN_CONTROL";
inline constexpr char kErrUnavailable[] = "PRISM_TASK_SPACE_UNAVAILABLE";
inline constexpr char kErrWebContents[] = "PRISM_WEB_CONTENTS_UNAVAILABLE";
inline constexpr char kErrSnapshotFailed[] = "PRISM_SNAPSHOT_FAILED";

// Maps a SpaceManager error to its wire code.
const char* WireCodeForSpaceError(SpaceManager::Error error);

}  // namespace prism

#endif  // PRISM_BROWSER_DEVTOOLS_PRISM_DOMAIN_HANDLER_H_
