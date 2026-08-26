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
    : DevToolsDomainHandler(Prism::Metainfo::domainName) {}

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
  space_manager_.RemoveTab(target_id);
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
    space_manager_.RemoveTab(target_id);
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
      .Build();
}

Response PrismDomainHandler::ListTaskSpaces(
    std::unique_ptr<protocol::Array<Prism::TaskSpace>>* out_taskSpaces) {
  auto list = std::make_unique<protocol::Array<Prism::TaskSpace>>();
  for (const auto& space : space_manager_.List()) {
    list->emplace_back(ToWire(space));
  }
  *out_taskSpaces = std::move(list);
  return Response::Success();
}

Response PrismDomainHandler::CreateTaskSpace(
    const String& in_name,
    std::unique_ptr<Prism::TaskSpace>* out_taskSpace) {
  auto result =
      space_manager_.Create(in_name, prism::SpaceManager::Owner::kAgent);
  *out_taskSpace = ToWire(*result.space);
  return Response::Success();
}

Response PrismDomainHandler::UseTaskSpace(
    int in_id,
    std::unique_ptr<Prism::TaskSpace>* out_taskSpace) {
  auto result = space_manager_.Use(in_id);
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
  auto result = space_manager_.Claim(in_id, in_name.value_or(""));
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
  if (auto error = RequireSelectedSpace()) {
    return *error;
  }
  *out_done = true;
  return Response::Success();
}

Response PrismDomainHandler::CloseTaskSpace(bool* out_done) {
  if (auto error = RequireSelectedSpace()) {
    return *error;
  }
  // Destroy the space's agent tabs before removing the space itself
  // (DestroyTab prunes the per-tab bookkeeping).
  if (const auto* space = space_manager_.Find(*selected_space_id_)) {
    std::vector<std::string> target_ids;
    target_ids.reserve(space->tabs.size());
    for (const auto& tab : space->tabs) {
      target_ids.push_back(tab.target_id);
    }
    for (const auto& target_id : target_ids) {
      DestroyTab(target_id);
    }
  }
  space_manager_.Close(*selected_space_id_);
  selected_space_id_.reset();
  *out_done = true;
  return Response::Success();
}

Response PrismDomainHandler::HandOffTaskSpace(
    std::unique_ptr<Prism::TaskSpace>* out_taskSpace) {
  if (auto error = RequireSelectedSpace()) {
    return *error;
  }
  auto result = space_manager_.HandOff(*selected_space_id_);
  *out_taskSpace = ToWire(*result.space);
  return Response::Success();
}

Response PrismDomainHandler::TakeOverTaskSpace(
    std::unique_ptr<Prism::TaskSpace>* out_taskSpace) {
  if (auto error = RequireSelectedSpace()) {
    return *error;
  }
  auto result = space_manager_.TakeOver(*selected_space_id_);
  *out_taskSpace = ToWire(*result.space);
  return Response::Success();
}

Response PrismDomainHandler::ListTabs(
    std::unique_ptr<protocol::Array<Prism::TabInfo>>* out_tabs) {
  if (auto error = RequireSelectedSpace()) {
    return *error;
  }
  const auto* space = space_manager_.Find(*selected_space_id_);
  auto list = std::make_unique<protocol::Array<Prism::TabInfo>>();
  for (const auto& record : space->tabs) {
    auto it = tabs_.find(record.target_id);
    if (it == tabs_.end()) {
      continue;  // destroyed, bookkeeping reap pending
    }
    content::WebContents* web_contents = it->second.get();
    list->emplace_back(Prism::TabInfo::Create()
                           .SetTargetId(record.target_id)
                           .SetTitle(base::UTF16ToUTF8(web_contents->GetTitle()))
                           .SetUrl(web_contents->GetLastCommittedURL().spec())
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
  space_manager_.AddTab(*selected_space_id_, std::move(record));

  tab_observers_[target_id] =
      std::make_unique<TabObserver>(web_contents.get(), this);
  tabs_[target_id] = std::move(web_contents);

  *out_targetId = target_id;
  return Response::Success();
}

Response PrismDomainHandler::Snapshot(
    std::optional<String> in_scope,
    std::optional<bool> in_includeActionMarks,
    std::optional<bool> in_includeStableLocator,
    std::optional<int> in_maxResultLength,
    String* out_content,
    std::unique_ptr<protocol::Array<Prism::SnapshotRef>>* out_refs) {
  // Phase 3: kernel renderer (frame-tree AXTree composition). Until then the
  // adapter's JS composer remains the active path (ADR-003).
  if (auto error = RequireSelectedSpace()) {
    return *error;
  }
  return ErrorResponse(prism::kErrSnapshotFailed,
                       "kernel snapshot not yet implemented; use adapter "
                       "simulation mode");
}

Response PrismDomainHandler::GetBrowserVersion(
    String* out_currentVersion,
    bool* out_updateAvailable,
    std::optional<String>* out_latestVersion,
    std::optional<bool>* out_mandatory) {
  *out_currentVersion = prism::kPrismVersionPlaceholder;
  *out_updateAvailable = false;
  return Response::Success();
}

std::optional<Response> PrismDomainHandler::RequireSelectedSpace() const {
  if (!selected_space_id_.has_value()) {
    return ErrorResponse(prism::kErrNotSelected, "no task space selected");
  }
  const auto* space = space_manager_.Find(*selected_space_id_);
  if (!space) {
    return ErrorResponse(prism::kErrNotFound, "selected task space is gone");
  }
  if (space->ownership == prism::SpaceManager::Ownership::kAgentDelegatedToUser) {
    return ErrorResponse(prism::kErrUserInControl,
                         "task space is controlled by the user");
  }
  return std::nullopt;
}

}  // namespace content::protocol
