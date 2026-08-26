// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef PRISM_BROWSER_SNAPSHOT_SNAPSHOT_COMPOSER_H_
#define PRISM_BROWSER_SNAPSHOT_SNAPSHOT_COMPOSER_H_

#include <string>
#include <vector>

namespace prism {

// Pure-logic snapshot composer (kernel renderer, Phase 3; ADR-003).
//
// Input: one AXNode list per local-root frame (the DevTools
// Accessibility.getFullAXTree shape, translated), plus the iframe-owner links
// between frames. Output: the contract snapshot text ({content, refs}) with
// cross-process iframes spliced inline — the thing the JS composer
// (host/src/snapshot.js) structurally cannot do.
//
// No content/ dependencies on purpose: fully unit-testable in //prism.
//
// Text format (superset of the JS composer's; byte parity is not a goal):
//   <indent>- <role>[ "<name>"|"<value>"] [ref=<backendNodeId>, loc=..., url=...]
// Ignored AX nodes are skipped but their children are hoisted (same as the JS
// composer).

struct SnapshotAXNode {
  std::string id;         // AX node id (string, as on the wire)
  std::string parent_id;  // empty for the frame-local root
  bool ignored = false;
  std::string role;
  std::string name;
  std::string value;
  std::string url;        // link target when exposed (AXProperty "url")
  int backend_node_id = 0;  // CDP backendDOMNodeId; 0 = none
  std::vector<std::string> child_ids;

  // Filled only when viewport filtering is requested (getBoxModel result in
  // the frame's local coordinate space).
  bool has_box = false;
  float box_x = 0, box_y = 0, box_w = 0, box_h = 0;
};

struct SnapshotFrameData {
  std::string frame_token;      // diagnostics only
  int parent_frame_index = -1;  // -1 for the top frame
  int owner_backend_node_id = 0;  // backendNodeId of the <iframe> element in
                                  // the parent frame; 0 for the top frame
  std::vector<SnapshotAXNode> nodes;
};

struct SnapshotOptions {
  // Contract §4.1. Defaults mirror page.snapshot().
  bool include_action_marks = true;    // accepted; currently a no-op (same as
                                       // the JS composer)
  bool include_stable_locator = true;  // loc= annotations
  int max_result_length = 0;           // 0 = no truncation
  bool only_within_viewport = false;   // scope filter (needs box data)
  float viewport_width = 0;
  float viewport_height = 0;
};

struct SnapshotRefEntry {
  int backend_node_id = 0;
  std::string role;
  std::string name;
};

struct SnapshotResult {
  std::string content;
  std::vector<SnapshotRefEntry> refs;
};

// Composes the frames (index 0 = top frame) into the contract snapshot.
// Frames whose owner node cannot be found in the parent's composed tree are
// dropped (e.g. display:none iframes produce no AX node to attach to).
SnapshotResult ComposeSnapshot(const std::vector<SnapshotFrameData>& frames,
                               const SnapshotOptions& options);

}  // namespace prism

#endif  // PRISM_BROWSER_SNAPSHOT_SNAPSHOT_COMPOSER_H_
