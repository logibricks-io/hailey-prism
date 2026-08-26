// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef PRISM_BROWSER_SPACES_SPACE_MANAGER_H_
#define PRISM_BROWSER_SPACES_SPACE_MANAGER_H_

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "base/types/pass_key.h"

namespace prism {

// The Space registry and ownership state machine (kernel side).
//
// Pure logic, deliberately free of content/ dependencies so it can be unit
// tested standalone. Window/WebContents wiring lives in the DevTools domain
// handler. Semantics mirror docs/binding-contract.md §2.3 and must stay in
// sync with the adapter's simulation (host/src/spaces.js).
class SpaceManager {
 public:
  enum class Owner { kAgent, kUser };
  enum class Ownership {
    kAgent,                // agent controls the space
    kUser,                 // the user's own space; agents may select but not drive
    kAgentDelegatedToUser, // handed off; user in control until take-over
  };

  struct Space {
    int id = 0;
    std::string task_id;  // string form of id, kept for wire symmetry
    std::string name;
    Owner created_by = Owner::kAgent;
    Ownership ownership = Ownership::kAgent;
    std::vector<std::string> recent_tab_titles;
  };

  enum class Error {
    kNone,
    kNotFound,      // PRISM_TASK_SPACE_NOT_FOUND
    kUserInControl, // PRISM_TASK_SPACE_USER_IN_CONTROL
  };

  struct Result {
    Error error = Error::kNone;
    std::optional<Space> space;
  };

  SpaceManager();
  ~SpaceManager();

  std::vector<Space> List() const;

  Result Create(const std::string& name, Owner created_by);

  // Select is performed by the domain handler (per-session state); the
  // manager only answers the ownership question. Use() mirrors the binding
  // rule: selecting a kUser space fails with kUserInControl.
  Result Use(int id) const;

  // Converts a user-owned space to agent-owned.
  Result Claim(int id, const std::string& name_if_needed);

  // Agent -> user handoff: kAgent becomes kAgentDelegatedToUser.
  Result HandOff(int id);

  // User -> agent take-over: kAgentDelegatedToUser becomes kAgent.
  // No ownership check (contract §2.3).
  Result TakeOver(int id);

  // Closes (destroys) a space. Returns kNotFound when absent.
  Error Close(int id);

  const Space* Find(int id) const;

 private:
  std::map<int, Space> spaces_;
  int next_id_ = 1;
};

}  // namespace prism

#endif  // PRISM_BROWSER_SPACES_SPACE_MANAGER_H_
