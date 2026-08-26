// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef PRISM_BROWSER_DEVTOOLS_PRISM_DOMAIN_HANDLER_H_
#define PRISM_BROWSER_DEVTOOLS_PRISM_DOMAIN_HANDLER_H_

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>

#include "base/memory/weak_ptr.h"
#include "content/browser/devtools/protocol/devtools_domain_handler.h"
#include "content/browser/devtools/protocol/prism.h"
#include "content/public/browser/web_contents_delegate.h"
#include "prism/browser/spaces/space_manager.h"

namespace content {
class WebContents;
}  // namespace content

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

}  // namespace prism

namespace content::protocol {

class PrismDomainHandler : public DevToolsDomainHandler,
                           public Prism::Backend,
                           public content::WebContentsDelegate {
 public:
  PrismDomainHandler();

  PrismDomainHandler(const PrismDomainHandler&) = delete;
  PrismDomainHandler& operator=(const PrismDomainHandler&) = delete;

  ~PrismDomainHandler() override;

  // DevToolsDomainHandler:
  void Wire(UberDispatcher* dispatcher) override;
  void SetRenderer(int process_host_id,
                   RenderFrameHostImpl* frame_host) override;

  // content::WebContentsDelegate (agent tabs):
  void CloseContents(content::WebContents* source) override;

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

  // Prism::Backend — Phase 4: space windows + agent state.
  Response ShowTaskSpace(int in_id, bool* out_done) override;
  Response SetAgentTaskState(const String& in_label, bool* out_done) override;
  Response AnimationHighlightMouseToPosition(int in_x,
                                             int in_y,
                                             bool* out_done) override;

  // Prism::Backend — snapshot (Phase 3 kernel renderer; async — registered
  // with "async" in protocol_config.json).
  void Snapshot(std::optional<String> in_scope,
                std::optional<bool> in_includeActionMarks,
                std::optional<bool> in_includeStableLocator,
                std::optional<int> in_maxResultLength,
                std::unique_ptr<SnapshotCallback> callback) override;

  // Prism::Backend — version.
  Response GetBrowserVersion(String* out_currentVersion,
                             bool* out_updateAvailable,
                             std::optional<String>* out_latestVersion,
                             std::optional<bool>* out_mandatory) override;

  // DevToolsSession policy hook (patch 0006): true when this raw CDP command
  // must be rejected because the selected space is user-controlled.
  // `out_error` gets the wire message (PRISM_ prefixed).
  bool ShouldBlockCommand(const std::string& method, std::string* out_error);

  // Target.getTargets filter (patch 0007): when a space is selected, the set
  // of that space's tab target ids; nullopt when nothing is selected (no
  // filtering — bootstrap).
  std::optional<std::set<std::string>> SelectedSpaceTargetIds() const;

 private:
  // Observes one agent tab so the space bookkeeping is pruned when the
  // WebContents dies without the handler initiating it (defined in the .cc).
  class TabObserver;

  // Builds the wire TaskSpace object from the manager record.
  static std::unique_ptr<Prism::TaskSpace> ToWire(
      const prism::SpaceManager::Space& space);

  // Returns a DispatchResponse error when this session has no usable selected
  // space (none selected / gone / user in control). The lifecycle commands
  // (complete/close/handOff/takeOver) pass allow_user_in_control=true: they
  // are the management channel through which control is handed over and
  // regained, so they must stay callable while the space is delegated — only
  // driving commands (createTab/listTabs/snapshot) are gated on ownership.
  std::optional<Response> RequireSelectedSpace(
      bool allow_user_in_control = false) const;

  // Destroys the agent tab: stops observing it, deletes the WebContents and
  // removes the space bookkeeping. No-op when the target id is unknown.
  void DestroyTab(const std::string& target_id);

  // TabObserver callback: the WebContents is already inside its destructor, so
  // ownership is dropped without deleting. The observer entry is reaped on the
  // next task turn to avoid mutating the observer list mid-notification.
  void OnTabWebContentsDestroyed(content::WebContents* web_contents);
  void ReapTabObserver(const std::string& target_id);

  raw_ptr<prism::SpaceManager> space_manager_;  // global singleton, UI thread
  std::optional<int> selected_space_id_;

  // Agent tabs created via Prism.createTab, owned by this handler/session and
  // keyed by DevToolsAgentHost target id. Observers are declared (and thus
  // destroyed) before the WebContents so teardown never re-enters the handler.
  std::map<std::string, std::unique_ptr<TabObserver>> tab_observers_;
  std::map<std::string, std::unique_ptr<content::WebContents>> tabs_;

  base::WeakPtrFactory<PrismDomainHandler> weak_factory_{this};
};

}  // namespace content::protocol

#endif  // PRISM_BROWSER_DEVTOOLS_PRISM_DOMAIN_HANDLER_H_
