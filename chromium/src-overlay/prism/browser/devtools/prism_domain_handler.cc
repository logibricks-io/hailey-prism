// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "prism/browser/devtools/prism_domain_handler.h"

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "content/browser/devtools/devtools_manager.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/devtools_manager_delegate.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "prism/browser/snapshot/snapshot_job.h"
#include "prism/browser/spaces/space_window_delegate.h"
#include "prism/version/prism_version_values.h"
#include "url/gurl.h"
#include "url/url_constants.h"

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

namespace content::protocol {

namespace {

Response ErrorResponse(const char* code, const std::string& detail) {
  return Response::ServerError(base::StrCat({code, ": ", detail}));
}

String OwnershipToWire(prism::SpaceManager::Ownership ownership) {
  switch (ownership) {
    case prism::SpaceManager::Ownership::kAgent:
      return "agent";
    case prism::SpaceManager::Ownership::kUser:
      return "user";
    case prism::SpaceManager::Ownership::kAgentDelegatedToUser:
      return "agentDelegatedToUser";
  }
  return "agent";
}

String CreatedByToWire(prism::SpaceManager::Owner owner) {
  return owner == prism::SpaceManager::Owner::kAgent ? "agent" : "user";
}

}  // namespace

// Watches one agent tab. The only expected external destruction trigger is
// page-initiated window.close() (routed through CloseContents, which already
// prunes deliberately); the callback below is the safety net for anything
// else, e.g. BrowserContext shutdown.
class PrismDomainHandler::TabObserver : public content::WebContentsObserver {
 public:
  TabObserver(content::WebContents* web_contents, PrismDomainHandler* handler)
      : content::WebContentsObserver(web_contents), handler_(handler) {}

  void WebContentsDestroyed() override {
    handler_->OnTabWebContentsDestroyed(web_contents());
  }

 private:
  raw_ptr<PrismDomainHandler> handler_;
};

PrismDomainHandler::PrismDomainHandler()
    : DevToolsDomainHandler(Prism::Metainfo::domainName),
      space_manager_(prism::SpaceManager::GetInstance()) {}

PrismDomainHandler::~PrismDomainHandler() {
  // Stop observing before the WebContents are torn down (member destruction
  // order already guarantees this; doing it explicitly keeps the invariant
  // independent of declaration order).
  tab_observers_.clear();
}

void PrismDomainHandler::Wire(UberDispatcher* dispatcher) {
  Prism::Dispatcher::wire(dispatcher, this);
}

void PrismDomainHandler::SetRenderer(int process_host_id,
                                     RenderFrameHostImpl* frame_host) {
  // Browser-target domain: no renderer association.
}

void PrismDomainHandler::CloseContents(content::WebContents* source) {
  // Page-initiated window.close() on an agent tab: destroy it like an explicit
  // close. Bookkeeping pruning happens in DestroyTab.
  for (const auto& [target_id, web_contents] : tabs_) {
    if (web_contents.get() == source) {
      DestroyTab(target_id);
      return;
    }
  }
}

void PrismDomainHandler::DestroyTab(const std::string& target_id) {
  // Erase the observer first: deleting the WebContents fires
  // WebContentsDestroyed, which must not re-enter here.
  tab_observers_.erase(target_id);
  tabs_.erase(target_id);
  space_manager_->RemoveTab(target_id);
}

void PrismDomainHandler::OnTabWebContentsDestroyed(
    content::WebContents* web_contents) {
  for (auto it = tabs_.begin(); it != tabs_.end(); ++it) {
    if (it->second.get() != web_contents) {
      continue;
    }
    // The WebContents is already inside ~WebContentsImpl: drop ownership
    // without deleting, prune the bookkeeping, and reap the observer entry on
    // the next task turn (it is currently executing the notification).
    const std::string target_id = it->first;
    it->second.release();
    tabs_.erase(it);
    space_manager_->RemoveTab(target_id);
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&PrismDomainHandler::ReapTabObserver,
                       weak_factory_.GetWeakPtr(), target_id));
    return;
  }
}

void PrismDomainHandler::ReapTabObserver(const std::string& target_id) {
  tab_observers_.erase(target_id);
}

std::unique_ptr<Prism::TaskSpace> PrismDomainHandler::ToWire(
    const prism::SpaceManager::Space& space) {
  auto titles = std::make_unique<protocol::Array<String>>();
  for (const auto& title : space.recent_tab_titles) {
    titles->emplace_back(title);
  }
  return Prism::TaskSpace::Create()
      .SetId(space.id)
      .SetTaskId(space.task_id)
      .SetName(space.name)
      .SetCreatedBy(CreatedByToWire(space.created_by))
      .SetOwnership(OwnershipToWire(space.ownership))
      .SetRecentTabTitles(std::move(titles))
      .SetAgentTaskState(space.agent_task_state)
      .SetWindowShown(space.window_shown)
      .Build();
}

Response PrismDomainHandler::ListTaskSpaces(
    std::unique_ptr<protocol::Array<Prism::TaskSpace>>* out_taskSpaces) {
  auto list = std::make_unique<protocol::Array<Prism::TaskSpace>>();
  for (const auto& space : space_manager_->List()) {
    list->emplace_back(ToWire(space));
  }
  *out_taskSpaces = std::move(list);
  return Response::Success();
}

Response PrismDomainHandler::CreateTaskSpace(
    const String& in_name,
    std::unique_ptr<Prism::TaskSpace>* out_taskSpace) {
  auto result =
      space_manager_->Create(in_name, prism::SpaceManager::Owner::kAgent);
  *out_taskSpace = ToWire(*result.space);
  return Response::Success();
}

Response PrismDomainHandler::UseTaskSpace(
    int in_id,
    std::unique_ptr<Prism::TaskSpace>* out_taskSpace) {
  auto result = space_manager_->Use(in_id);
  if (result.error != prism::SpaceManager::Error::kNone) {
    return ErrorResponse(
        prism::WireCodeForSpaceError(result.error),
        base::StrCat({"task space: ", base::NumberToString(in_id)}));
  }
  selected_space_id_ = in_id;
  *out_taskSpace = ToWire(*result.space);
  return Response::Success();
}

Response PrismDomainHandler::ClaimTaskSpace(
    int in_id,
    std::optional<String> in_name,
    std::unique_ptr<Prism::TaskSpace>* out_taskSpace) {
  auto result = space_manager_->Claim(in_id, in_name.value_or(""));
  if (result.error != prism::SpaceManager::Error::kNone) {
    return ErrorResponse(
        prism::WireCodeForSpaceError(result.error),
        base::StrCat({"task space: ", base::NumberToString(in_id)}));
  }
  selected_space_id_ = in_id;
  *out_taskSpace = ToWire(*result.space);
  return Response::Success();
}

Response PrismDomainHandler::CompleteTaskSpace(bool* out_done) {
  if (auto error = RequireSelectedSpace(/*allow_user_in_control=*/true)) {
    return *error;
  }
  *out_done = true;
  return Response::Success();
}

Response PrismDomainHandler::CloseTaskSpace(bool* out_done) {
  if (auto error = RequireSelectedSpace(/*allow_user_in_control=*/true)) {
    return *error;
  }
  // Destroy the space's agent tabs before removing the space itself
  // (DestroyTab prunes the per-tab bookkeeping).
  if (const auto* space = space_manager_->Find(*selected_space_id_)) {
    std::vector<std::string> target_ids;
    target_ids.reserve(space->tabs.size());
    for (const auto& tab : space->tabs) {
      target_ids.push_back(tab.target_id);
    }
    for (const auto& target_id : target_ids) {
      DestroyTab(target_id);
    }
  }
  space_manager_->Close(*selected_space_id_);
  selected_space_id_.reset();
  *out_done = true;
  return Response::Success();
}

Response PrismDomainHandler::HandOffTaskSpace(
    std::unique_ptr<Prism::TaskSpace>* out_taskSpace) {
  if (auto error = RequireSelectedSpace(/*allow_user_in_control=*/true)) {
    return *error;
  }
  auto result = space_manager_->HandOff(*selected_space_id_);
  *out_taskSpace = ToWire(*result.space);
  return Response::Success();
}

Response PrismDomainHandler::TakeOverTaskSpace(
    std::unique_ptr<Prism::TaskSpace>* out_taskSpace) {
  if (auto error = RequireSelectedSpace(/*allow_user_in_control=*/true)) {
    return *error;
  }
  auto result = space_manager_->TakeOver(*selected_space_id_);
  *out_taskSpace = ToWire(*result.space);
  return Response::Success();
}

Response PrismDomainHandler::ListTabs(
    std::unique_ptr<protocol::Array<Prism::TabInfo>>* out_tabs) {
  if (auto error = RequireSelectedSpace()) {
    return *error;
  }
  const auto* space = space_manager_->Find(*selected_space_id_);
  auto list = std::make_unique<protocol::Array<Prism::TabInfo>>();
  for (const auto& record : space->tabs) {
    // Resolve live state globally, not via this session's ownership map: the
    // registry is global (Phase 4), and a tab may belong to another client or
    // have been moved into a visible window — its agent host survives both.
    scoped_refptr<content::DevToolsAgentHost> host =
        content::DevToolsAgentHost::GetForId(record.target_id);
    content::WebContents* web_contents = host ? host->GetWebContents() : nullptr;
    if (!web_contents) {
      continue;  // destroyed, bookkeeping reap pending
    }
    const std::string title = base::UTF16ToUTF8(web_contents->GetTitle());
    const std::string url = web_contents->GetLastCommittedURL().spec();
    // Keep the global bookkeeping fresh for cross-session readers (the
    // management page reads SpaceManager directly, without live WebContents).
    space_manager_->UpdateTab(record.target_id, title, url);
    list->emplace_back(Prism::TabInfo::Create()
                           .SetTargetId(record.target_id)
                           .SetTitle(title)
                           .SetUrl(url)
                           .SetActive(record.active)
                           .Build());
  }
  *out_tabs = std::move(list);
  return Response::Success();
}

Response PrismDomainHandler::CreateTab(const String& in_url,
                                       String* out_targetId) {
  if (auto error = RequireSelectedSpace()) {
    return *error;
  }

  GURL url(in_url);
  if (in_url.empty()) {
    url = GURL(url::kAboutBlankURL);
  }
  if (!url.is_valid()) {
    return ErrorResponse(prism::kErrWebContents,
                         base::StrCat({"invalid url: ", in_url}));
  }

  // The pipe-created browser target carries no BrowserContext
  // (DevToolsPipeHandler passes nullptr to CreateForBrowser), so the default
  // context of the current profile comes from the DevToolsManager delegate —
  // the same path BrowserHandler::FindBrowserContext uses.
  content::DevToolsManagerDelegate* delegate =
      content::DevToolsManager::GetInstance()->delegate();
  content::BrowserContext* browser_context =
      delegate ? delegate->GetDefaultBrowserContext() : nullptr;
  if (!browser_context) {
    return ErrorResponse(prism::kErrBrowserUnavailable,
                         "no default browser context");
  }

  content::WebContents::CreateParams create_params(browser_context);
  std::unique_ptr<content::WebContents> web_contents =
      content::WebContents::Create(create_params);
  if (!web_contents) {
    return ErrorResponse(prism::kErrWebContents, "WebContents::Create failed");
  }
  // Handles page-initiated window.close(); also required so the WebContents is
  // not leaked on unhandled delegate calls.
  web_contents->SetDelegate(this);

  content::NavigationController::LoadURLParams load_params(url);
  web_contents->GetController().LoadURLWithParams(load_params);

  const std::string target_id =
      content::DevToolsAgentHost::GetOrCreateFor(web_contents.get())->GetId();

  prism::SpaceManager::TabRecord record;
  record.target_id = target_id;
  record.url = url.spec();
  space_manager_->AddTab(*selected_space_id_, std::move(record));

  tab_observers_[target_id] =
      std::make_unique<TabObserver>(web_contents.get(), this);
  tabs_[target_id] = std::move(web_contents);

  *out_targetId = target_id;
  return Response::Success();
}

Response PrismDomainHandler::ShowTaskSpace(int in_id, bool* out_done) {
  auto* delegate = prism::GetSpaceWindowDelegate();
  if (!delegate) {
    return ErrorResponse(prism::kErrOperationFailed,
                         "space windows unsupported in this build");
  }
  if (!space_manager_->Find(in_id)) {
    return ErrorResponse(prism::kErrNotFound,
                         base::StrCat({"task space: ", base::NumberToString(in_id)}));
  }

  // Hand the space's windowless tabs to the chrome layer. Ownership moves to
  // the browser window's tab strip; the bookkeeping records stay (they are
  // the space's tab list, whatever owns the WebContents).
  std::vector<std::unique_ptr<content::WebContents>> moving;
  const auto* space = space_manager_->Find(in_id);
  for (const auto& record : space->tabs) {
    auto it = tabs_.find(record.target_id);
    if (it != tabs_.end()) {
      moving.push_back(std::move(it->second));
      tabs_.erase(it);
    }
  }
  delegate->ShowTaskSpace(in_id, std::move(moving));
  space_manager_->SetWindowShown(in_id, true);
  *out_done = true;
  return Response::Success();
}

Response PrismDomainHandler::SetAgentTaskState(const String& in_label,
                                               bool* out_done) {
  if (auto error = RequireSelectedSpace(/*allow_user_in_control=*/true)) {
    return *error;
  }
  auto result = space_manager_->SetAgentTaskState(*selected_space_id_, in_label);
  if (result != prism::SpaceManager::Error::kNone) {
    return ErrorResponse(prism::WireCodeForSpaceError(result),
                         "selected task space is gone");
  }
  *out_done = true;
  return Response::Success();
}

Response PrismDomainHandler::AnimationHighlightMouseToPosition(int in_x,
                                                               int in_y,
                                                               bool* out_done) {
  if (auto error = RequireSelectedSpace()) {
    return *error;
  }
  if (auto* delegate = prism::GetSpaceWindowDelegate()) {
    delegate->AnimateClickHighlight(*selected_space_id_, in_x, in_y);
  }
  *out_done = true;
  return Response::Success();
}

bool PrismDomainHandler::ShouldBlockCommand(const std::string& method,
                                            std::string* out_error) {
  if (!selected_space_id_.has_value()) {
    return false;  // bootstrap: no space selected yet, no policy to enforce
  }
  const auto* space = space_manager_->Find(*selected_space_id_);
  if (!space ||
      space->ownership != prism::SpaceManager::Ownership::kAgentDelegatedToUser) {
    return false;
  }
  // Driving (page-affecting) command prefixes; queries pass through.
  static constexpr const char* kDrivingPrefixes[] = {
      "Input.",        "Page.navigate",   "Page.reload",
      "Runtime.evaluate", "Runtime.callFunctionOn", "Runtime.callMethodOn",
      "Target.createTarget", "Target.closeTarget", "Target.activateTarget",
      "Browser.setDownloadBehavior", "Emulation.set", "Network.set",
      "Fetch.enable",  "Debugger.",
  };
  for (const char* prefix : kDrivingPrefixes) {
    if (method.starts_with(prefix)) {
      *out_error = base::StrCat(
          {prism::kErrUserInControl,
           ": command blocked while the task space is controlled by the user: ",
           method});
      return true;
    }
  }
  return false;
}

std::optional<std::set<std::string>>
PrismDomainHandler::SelectedSpaceTargetIds() const {
  if (!selected_space_id_.has_value()) {
    return std::nullopt;
  }
  const auto* space = space_manager_->Find(*selected_space_id_);
  if (!space) {
    return std::nullopt;
  }
  std::set<std::string> ids;
  for (const auto& tab : space->tabs) {
    ids.insert(tab.target_id);
  }
  return ids;
}

void PrismDomainHandler::Snapshot(
    std::optional<String> in_scope,
    std::optional<bool> in_includeActionMarks,
    std::optional<bool> in_includeStableLocator,
    std::optional<int> in_maxResultLength,
    std::unique_ptr<SnapshotCallback> callback) {
  // Phase 3 kernel renderer (ADR-003): per-local-root AX trees via internal
  // DevTools sessions, cross-process iframes spliced inline.
  if (auto error = RequireSelectedSpace()) {
    callback->sendFailure(*error);
    return;
  }

  // A freshly selected space may have no tab yet. Resolve an empty snapshot
  // (not an error): the harness's agent-control probe reads this as "agent
  // owns an empty space".
  const auto* space = space_manager_->Find(*selected_space_id_);
  content::WebContents* web_contents = nullptr;
  for (const auto& record : space->tabs) {
    if (record.active) {
      auto it = tabs_.find(record.target_id);
      if (it != tabs_.end()) {
        web_contents = it->second.get();
      }
      break;
    }
  }
  if (!web_contents) {
    callback->sendSuccess(
        String(), std::make_unique<protocol::Array<Prism::SnapshotRef>>());
    return;
  }

  prism::SnapshotOptions options;
  options.only_within_viewport = in_scope == "only_within_viewport";
  options.include_action_marks = in_includeActionMarks.value_or(true);
  options.include_stable_locator = in_includeStableLocator.value_or(true);
  options.max_result_length = in_maxResultLength.value_or(0);

  // The job self-owns: it stays alive through the attached agent hosts and
  // deletes itself after firing the callback (see SnapshotJob::Finish/Fail).
  auto* job = new prism::SnapshotJob(
      web_contents, options,
      base::BindOnce(
          [](std::unique_ptr<SnapshotCallback> callback,
             std::optional<prism::SnapshotResult> result,
             const std::string& error) {
            if (!result) {
              callback->sendFailure(ErrorResponse(prism::kErrSnapshotFailed,
                                                  error));
              return;
            }
            auto refs =
                std::make_unique<protocol::Array<Prism::SnapshotRef>>();
            for (const auto& ref : result->refs) {
              refs->emplace_back(Prism::SnapshotRef::Create()
                                     .SetBackendNodeId(ref.backend_node_id)
                                     .SetRole(ref.role)
                                     .SetName(ref.name)
                                     .Build());
            }
            callback->sendSuccess(result->content, std::move(refs));
          },
          std::move(callback)));
  job->Run();
}

Response PrismDomainHandler::GetBrowserVersion(
    String* out_currentVersion,
    bool* out_updateAvailable,
    std::optional<String>* out_latestVersion,
    std::optional<bool>* out_mandatory) {
  // Brand + pinned version from the generated prism_version_values.h
  // (//prism:generate_version over //chrome/VERSION and the active BRANDING).
  *out_currentVersion =
      base::StrCat({PRISM_PRODUCT_NAME, "/", PRISM_PRODUCT_VERSION});
  *out_updateAvailable = false;
  return Response::Success();
}

std::optional<Response> PrismDomainHandler::RequireSelectedSpace(
    bool allow_user_in_control) const {
  if (!selected_space_id_.has_value()) {
    return ErrorResponse(prism::kErrNotSelected, "no task space selected");
  }
  const auto* space = space_manager_->Find(*selected_space_id_);
  if (!space) {
    return ErrorResponse(prism::kErrNotFound, "selected task space is gone");
  }
  if (!allow_user_in_control &&
      space->ownership == prism::SpaceManager::Ownership::kAgentDelegatedToUser) {
    return ErrorResponse(prism::kErrUserInControl,
                         "task space is controlled by the user");
  }
  return std::nullopt;
}

}  // namespace content::protocol
