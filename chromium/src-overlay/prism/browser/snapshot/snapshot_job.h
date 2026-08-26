// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef PRISM_BROWSER_SNAPSHOT_SNAPSHOT_JOB_H_
#define PRISM_BROWSER_SNAPSHOT_SNAPSHOT_JOB_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "content/public/browser/devtools_agent_host_client.h"
#include "prism/browser/snapshot/snapshot_composer.h"

namespace content {
class DevToolsAgentHost;
class FrameTreeNode;
class WebContents;
}  // namespace content

namespace prism {

// Drives one Prism.snapshot call (Phase 3 kernel renderer; ADR-003).
//
// For each local-root frame of the tab's frame tree (the top frame plus every
// cross-process (OOPIF) iframe root), the job attaches an internal DevTools
// session to that frame's agent host and pulls the renderer's AX tree with
// Accessibility.getFullAXTree — the same renderer-side serializer the CDP
// command uses, so nodes arrive with real backendDOMNodeIds (contract §4.3).
// Cross-process children are spliced under their owner <iframe> element
// (DOM.getFrameOwner) — the expansion the JS path cannot do (ADR-003).
//
// With scope=only_within_viewport, each actionable node's box is resolved
// (DOM.getBoxModel, in that frame's local coordinates — an approximation for
// nested frames, documented) and off-viewport elements are dropped.
//
// All work runs on the UI thread. The job self-destructs via the callback.
class SnapshotJob : public content::DevToolsAgentHostClient {
 public:
  // Exactly one of result/error is set. PRISM_* prefixing happens in the
  // domain handler.
  using DoneCallback =
      base::OnceCallback<void(std::optional<SnapshotResult> result,
                              const std::string& error)>;

  SnapshotJob(content::WebContents* web_contents,
              const SnapshotOptions& options,
              DoneCallback callback);
  ~SnapshotJob() override;

  SnapshotJob(const SnapshotJob&) = delete;
  SnapshotJob& operator=(const SnapshotJob&) = delete;

  void Run();

  // content::DevToolsAgentHostClient:
  void DispatchProtocolMessage(content::DevToolsAgentHost* agent_host,
                               base::span<const uint8_t> message) override;
  void AgentHostClosed(content::DevToolsAgentHost* agent_host) override;
  std::string GetTypeForMetrics() override;

 private:
  struct FrameWork {
    raw_ptr<content::FrameTreeNode> node =
        nullptr;  // local root's frame tree node
    int parent_local_root_index = -1;        // index into frames_; -1 for top
    std::string frame_token;                 // devtools frame token
    scoped_refptr<content::DevToolsAgentHost> agent_host;
    SnapshotFrameData data;
  };

  // Frame collection: preorder local roots of the primary frame tree.
  void CollectFrames();

  // Per-frame pipeline (sequential): attach → [viewport dims] → getFullAXTree
  // → [getFrameOwner on the parent's session] → [per-ref getBoxModel] → next.
  void ProcessNextFrame();
  void RequestAXTree();
  void OnViewportDims(bool ok, const std::string& body);
  void OnAXTree(bool ok, const std::string& body);
  void OnFrameOwner(bool ok, const std::string& body);
  void MaybeQueryBoxes();
  void SendNextBoxModelQuery();
  void OnBoxModel(size_t node_index, bool ok, const std::string& body);
  void FinishFrame();

  // Wire helpers: ids are monotonically increasing; responses correlate by id.
  void SendCommand(content::DevToolsAgentHost* host,
                   const std::string& method,
                   const std::string& params_json,
                   base::OnceCallback<void(bool ok, const std::string& body)>
                       on_response);
  void DetachAll();

  void Finish(SnapshotResult result);
  void Fail(const std::string& error);

  raw_ptr<content::WebContents> web_contents_;
  SnapshotOptions options_;
  DoneCallback callback_;

  std::vector<std::unique_ptr<FrameWork>> frames_;
  size_t current_frame_ = 0;
  // DOM.getBoxModel queue for the current frame (viewport scope only).
  std::vector<size_t> pending_box_nodes_;
  size_t current_box_node_ = 0;

  int next_request_id_ = 1;
  std::map<int, base::OnceCallback<void(bool, const std::string&)>>
      pending_requests_;

  base::OneShotTimer watchdog_;
  base::WeakPtrFactory<SnapshotJob> weak_factory_{this};
};

}  // namespace prism

#endif  // PRISM_BROWSER_SNAPSHOT_SNAPSHOT_JOB_H_
