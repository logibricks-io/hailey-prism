// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "prism/browser/devtools/prism_domain_handler.h"

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"

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

PrismDomainHandler::PrismDomainHandler()
    : DevToolsDomainHandler(Prism::Metainfo::domainName) {}
PrismDomainHandler::~PrismDomainHandler() = default;

void PrismDomainHandler::Wire(UberDispatcher* dispatcher) {
  Prism::Dispatcher::wire(dispatcher, this);
}

void PrismDomainHandler::SetRenderer(int process_host_id,
                                     RenderFrameHostImpl* frame_host) {
  // Browser-target domain: no renderer association.
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
  // TODO(phase-4): close the space's window group in chrome/ before removing.
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
  // TODO(phase-2): enumerate WebContents-based targets filtered by the
  // selected space's window group (DevToolsAgentHost list filtered by the
  // SpaceManager's target set).
  if (auto error = RequireSelectedSpace()) {
    return *error;
  }
  *out_tabs = std::make_unique<protocol::Array<Prism::TabInfo>>();
  return Response::Success();
}

Response PrismDomainHandler::CreateTab(const String& in_url,
                                       String* out_targetId) {
  // TODO(phase-2/4): create a tab in the selected space's window group via
  // chrome/'s Browser APIs (content/ cannot create browser windows).
  if (auto error = RequireSelectedSpace()) {
    return *error;
  }
  return ErrorResponse(prism::kErrOperationFailed,
                       "createTab not yet implemented in the kernel");
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
