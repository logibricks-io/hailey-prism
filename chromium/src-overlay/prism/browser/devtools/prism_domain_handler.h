// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef PRISM_BROWSER_DEVTOOLS_PRISM_DOMAIN_HANDLER_H_
#define PRISM_BROWSER_DEVTOOLS_PRISM_DOMAIN_HANDLER_H_

#include <memory>
#include <optional>

#include "content/browser/devtools/protocol/devtools_domain_handler.h"
#include "content/browser/devtools/protocol/prism.h"
#include "prism/browser/spaces/space_manager.h"

// Handles the custom Prism.* DevTools domain (prism/pdl/prism.pdl), the
// kernel-side half of the prism binding contract (docs/binding-contract.md).
//
// Registered for browser-target sessions in
// content/browser/devtools/browser_devtools_agent_host.cc (patch series).
// Per-session state: the selected Space id — the DevTools pipe connection is
// the session, which is what the adapter's "connection = state" model relies
// on (ADR-002).
//
// Failure convention: stable PRISM_* code prefix in the error message, e.g.
// "PRISM_TASK_SPACE_NOT_FOUND: task space not found: 7" (contract §3).

namespace prism {

// Stable wire codes (binding-contract.md §3.2).
inline constexpr char kErrBrowserUnavailable[] = "PRISM_BROWSER_UNAVAILABLE";
inline constexpr char kErrNotFound[] = "PRISM_TASK_SPACE_NOT_FOUND";
inline constexpr char kErrNotSelected[] = "PRISM_TASK_SPACE_NOT_SELECTED";
inline constexpr char kErrInactive[] = "PRISM_TASK_SPACE_INACTIVE";
inline constexpr char kErrUserInControl[] = "PRISM_TASK_SPACE_USER_IN_CONTROL";
inline constexpr char kErrUnavailable[] = "PRISM_TASK_SPACE_UNAVAILABLE";
inline constexpr char kErrWebContents[] = "PRISM_WEB_CONTENTS_UNAVAILABLE";
inline constexpr char kErrSnapshotFailed[] = "PRISM_SNAPSHOT_FAILED";
inline constexpr char kErrOperationFailed[] = "PRISM_OPERATION_FAILED";

// Maps a SpaceManager error to its wire code.
const char* WireCodeForSpaceError(SpaceManager::Error error);

// Placeholder until version_info is wired from the chrome layer (the content
// layer cannot see components/embedder_support). Pinned Chromium version +
// dev marker.
inline constexpr char kPrismVersionPlaceholder[] = "151.0.7922.174+prism-dev";

}  // namespace prism

namespace content::protocol {

class PrismDomainHandler : public DevToolsDomainHandler,
                           public Prism::Backend {
 public:
  PrismDomainHandler();

  PrismDomainHandler(const PrismDomainHandler&) = delete;
  PrismDomainHandler& operator=(const PrismDomainHandler&) = delete;

  ~PrismDomainHandler() override;

  // DevToolsDomainHandler:
  void Wire(UberDispatcher* dispatcher) override;
  void SetRenderer(int process_host_id,
                   RenderFrameHostImpl* frame_host) override;

  // Prism::Backend — task spaces.
  Response ListTaskSpaces(
      std::unique_ptr<protocol::Array<protocol::Prism::TaskSpace>>*
          out_taskSpaces) override;
  Response CreateTaskSpace(const String& in_name,
                           std::unique_ptr<protocol::Prism::TaskSpace>*
                               out_taskSpace) override;
  Response UseTaskSpace(int in_id,
                        std::unique_ptr<protocol::Prism::TaskSpace>*
                            out_taskSpace) override;
  Response ClaimTaskSpace(int in_id,
                          std::optional<String> in_name,
                          std::unique_ptr<protocol::Prism::TaskSpace>*
                              out_taskSpace) override;
  Response CompleteTaskSpace(bool* out_done) override;
  Response CloseTaskSpace(bool* out_done) override;
  Response HandOffTaskSpace(
      std::unique_ptr<protocol::Prism::TaskSpace>* out_taskSpace) override;
  Response TakeOverTaskSpace(
      std::unique_ptr<protocol::Prism::TaskSpace>* out_taskSpace) override;

  // Prism::Backend — tabs.
  Response ListTabs(std::unique_ptr<protocol::Array<protocol::Prism::TabInfo>>*
                        out_tabs) override;
  Response CreateTab(const String& in_url, String* out_targetId) override;

  // Prism::Backend — snapshot (Phase 3 kernel renderer; stub until then).
  Response Snapshot(
      std::optional<String> in_scope,
      std::optional<bool> in_includeActionMarks,
      std::optional<bool> in_includeStableLocator,
      std::optional<int> in_maxResultLength,
      String* out_content,
      std::unique_ptr<protocol::Array<protocol::Prism::SnapshotRef>>* out_refs)
      override;

  // Prism::Backend — version.
  Response GetBrowserVersion(String* out_currentVersion,
                             bool* out_updateAvailable,
                             std::optional<String>* out_latestVersion,
                             std::optional<bool>* out_mandatory) override;

 private:
  // Builds the wire TaskSpace object from the manager record.
  static std::unique_ptr<Prism::TaskSpace> ToWire(
      const prism::SpaceManager::Space& space);

  // Returns a DispatchResponse error when this session has no usable selected
  // space (none selected / gone / user in control).
  std::optional<Response> RequireSelectedSpace() const;

  prism::SpaceManager space_manager_;
  std::optional<int> selected_space_id_;
};

}  // namespace content::protocol

#endif  // PRISM_BROWSER_DEVTOOLS_PRISM_DOMAIN_HANDLER_H_
