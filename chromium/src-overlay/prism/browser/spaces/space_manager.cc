// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "prism/browser/spaces/space_manager.h"

#include <algorithm>

namespace prism {

SpaceManager::SpaceManager() = default;
SpaceManager::~SpaceManager() = default;

std::vector<SpaceManager::Space> SpaceManager::List() const {
  std::vector<Space> out;
  out.reserve(spaces_.size());
  for (const auto& [id, space] : spaces_) {
    out.push_back(space);
  }
  return out;
}

SpaceManager::Result SpaceManager::Create(const std::string& name,
                                          Owner created_by) {
  Space space;
  space.id = next_id_++;
  space.task_id = std::to_string(space.id);
  space.name = name.empty() ? "space-" + space.task_id : name;
  space.created_by = created_by;
  space.ownership =
      created_by == Owner::kAgent ? Ownership::kAgent : Ownership::kUser;
  spaces_.emplace(space.id, space);
  return Result{Error::kNone, space};
}

SpaceManager::Result SpaceManager::Use(int id) const {
  const Space* space = Find(id);
  if (!space) {
    return Result{Error::kNotFound, std::nullopt};
  }
  if (space->ownership == Ownership::kUser) {
    // Contract: a user-owned space cannot be driven by an agent.
    return Result{Error::kUserInControl, std::nullopt};
  }
  return Result{Error::kNone, *space};
}

SpaceManager::Result SpaceManager::Claim(int id,
                                         const std::string& name_if_needed) {
  auto it = spaces_.find(id);
  if (it == spaces_.end()) {
    return Result{Error::kNotFound, std::nullopt};
  }
  it->second.ownership = Ownership::kAgent;
  if (!name_if_needed.empty()) {
    it->second.name = name_if_needed;
  }
  return Result{Error::kNone, it->second};
}

SpaceManager::Result SpaceManager::HandOff(int id) {
  auto it = spaces_.find(id);
  if (it == spaces_.end()) {
    return Result{Error::kNotFound, std::nullopt};
  }
  if (it->second.ownership == Ownership::kAgent) {
    it->second.ownership = Ownership::kAgentDelegatedToUser;
  }
  return Result{Error::kNone, it->second};
}

SpaceManager::Result SpaceManager::TakeOver(int id) {
  auto it = spaces_.find(id);
  if (it == spaces_.end()) {
    return Result{Error::kNotFound, std::nullopt};
  }
  if (it->second.ownership == Ownership::kAgentDelegatedToUser) {
    it->second.ownership = Ownership::kAgent;
  }
  return Result{Error::kNone, it->second};
}

SpaceManager::Error SpaceManager::Close(int id) {
  return spaces_.erase(id) > 0 ? Error::kNone : Error::kNotFound;
}

const SpaceManager::Space* SpaceManager::Find(int id) const {
  auto it = spaces_.find(id);
  return it == spaces_.end() ? nullptr : &it->second;
}

SpaceManager::Error SpaceManager::AddTab(int space_id, TabRecord tab) {
  auto it = spaces_.find(space_id);
  if (it == spaces_.end()) {
    return Error::kNotFound;
  }
  for (auto& existing : it->second.tabs) {
    existing.active = false;
  }
  tab.active = true;
  it->second.tabs.push_back(std::move(tab));
  return Error::kNone;
}

bool SpaceManager::RemoveTab(const std::string& target_id) {
  for (auto& [id, space] : spaces_) {
    auto tab_it = std::find_if(
        space.tabs.begin(), space.tabs.end(),
        [&](const TabRecord& tab) { return tab.target_id == target_id; });
    if (tab_it == space.tabs.end()) {
      continue;
    }
    const bool was_active = tab_it->active;
    space.tabs.erase(tab_it);
    if (was_active && !space.tabs.empty()) {
      space.tabs.back().active = true;
    }
    return true;
  }
  return false;
}

}  // namespace prism
