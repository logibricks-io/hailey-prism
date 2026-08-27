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

  // A tab the space owns: an agent-created WebContents registered under the
  // space. Pure bookkeeping (no content/ types); the DevTools domain handler
  // owns the actual WebContents instances keyed by TabRecord::target_id.
  struct TabRecord {
    std::string target_id;  // DevToolsAgentHost id for the WebContents
    std::string url;
    std::string title;
    bool active = false;    // the most recently created/focused tab
  };

  struct Space {
    int id = 0;
    std::string task_id;  // string form of id, kept for wire symmetry
    std::string name;
    Owner created_by = Owner::kAgent;
    Ownership ownership = Ownership::kAgent;
    std::vector<std::string> recent_tab_titles;
    std::vector<TabRecord> tabs;
    // Agent-set status label (Prism.setAgentTaskState); shown in the
    // space window and on chrome://prism-spaces.
    std::string agent_task_state;
    // Whether a visible browser window currently hosts the space's tabs.
    bool window_shown = false;
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

  // Browser-process-wide registry (leaky singleton, UI thread). The space
  // registry must be global — per-session managers gave every client its own
  // id namespace, so spaces could neither be listed nor addressed across
  // sessions (and chrome-layer surfaces need to read them).
  static SpaceManager* GetInstance();

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

  // Stops the agent for good: any agent-held ownership becomes kUser, so
  // agents can neither drive nor select the space until they Claim it anew
  // (the PRISM_TASK_SPACE_INACTIVE family semantics).
  Result StopAgent(int id);

  // Closes (destroys) a space. Returns kNotFound when absent.
  Error Close(int id);

  const Space* Find(int id) const;

  // ---- tab bookkeeping ----

  // Records a tab owned by the space. The new tab becomes the active one.
  // Returns kNotFound when the space is absent.
  Error AddTab(int space_id, TabRecord tab);

  // Removes the tab from whichever space owns it. Returns false when no space
  // has the tab. When the removed tab was the active one, the most recently
  // recorded remaining tab (if any) becomes active.
  bool RemoveTab(const std::string& target_id);

  // Refreshes a tab's live title/url (from its WebContents at listTabs time).
  void UpdateTab(const std::string& target_id, const std::string& title,
                 const std::string& url);

  // Sets the agent-visible status label of the space.
  Error SetAgentTaskState(int space_id, const std::string& label);

  // Marks whether a visible window hosts the space.
  void SetWindowShown(int space_id, bool shown);

  // The space currently focused by user chrome (⌥S cycling, overview
  // highlight). 0 = the implicit default space: the user's main browsing
  // area, which has no Space record. Chrome-layer bookkeeping only; agents
  // never observe it.
  int focused_space_id() const { return focused_space_id_; }
  void set_focused_space_id(int id) { focused_space_id_ = id; }

 private:
  std::map<int, Space> spaces_;
  int next_id_ = 1;
  int focused_space_id_ = 0;
};

}  // namespace prism

#endif  // PRISM_BROWSER_SPACES_SPACE_MANAGER_H_
