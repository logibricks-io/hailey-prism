// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/ui/webui/prism_spaces/prism_spaces_ui.h"

#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/memory/ref_counted_memory.h"
#include "base/values.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/prism/prism_space_window_delegate.h"
#include "chrome/browser/prism/prism_spaces_ui_constants.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "prism/browser/spaces/space_manager.h"

namespace prism {

namespace {

// The management page: plain HTML/JS (no framework, no grit — served from a
// request filter to keep the patch surface minimal). Data comes from
// chrome://prism-spaces/data.json; actions go through chrome.send.
constexpr char kPageHtml[] = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><title>Prism Spaces</title>
<style>
  :root { color-scheme: dark; }
  body { font-family: -apple-system, system-ui, sans-serif; background: #17141f;
         color: #ece9f4; margin: 0; padding: 32px; }
  h1 { font-size: 20px; font-weight: 600; }
  h1 .mark { color: #b69cff; }
  .space { background: #211c2e; border: 1px solid #352c4a; border-radius: 12px;
           padding: 16px 20px; margin: 14px 0; }
  .row { display: flex; align-items: baseline; gap: 10px; flex-wrap: wrap; }
  .name { font-size: 15px; font-weight: 600; }
  .id, .ownership, .state, .window { color: #a79fbd; font-size: 12.5px; }
  .ownership.agentDelegatedToUser { color: #ffb86b; }
  .ownership.agent { color: #8fd3a5; }
  .state::before { content: "\2014\2013 "; color: #b69cff; }
  .tabs { margin: 10px 0 0 4px; color: #cfc8e3; font-size: 12.5px; }
  .tabs div { padding: 2px 0; }
  .tabs .active::before { content: "\25CF "; color: #b69cff; }
  .actions { margin-top: 10px; display: flex; gap: 8px; }
  button { background: #322948; color: #ece9f4; border: 1px solid #453a63;
           border-radius: 8px; padding: 5px 12px; font-size: 12.5px;
           cursor: pointer; }
  button:hover { background: #3d3260; }
  .empty { color: #a79fbd; }
</style></head><body>
<h1><span class="mark">Prism</span> Spaces</h1>
<div id="list" class="empty">Loading…</div>
<script src="app.js"></script></body></html>)HTML";

constexpr char kAppJs[] = R"JS(function action(id, kind) {
  chrome.send("spaceAction", [id, kind]);
  setTimeout(refresh, 250);
}
window.prismSpaces = { onData };
function span(parent, className, text) {
  const el = document.createElement("span");
  if (className) el.className = className;
  if (text !== undefined) el.textContent = text;
  parent.appendChild(el);
  return el;
}
function onData(spacesJson) {
  const spaces = JSON.parse(spacesJson);
  const list = document.getElementById("list");
  list.replaceChildren();
  if (!spaces.length) {
    list.className = "empty";
    list.textContent = "No task spaces yet. Agents create them via taskSpaces.new(...).";
    return;
  }
  list.className = "";
  for (const space of spaces) {
    const el = document.createElement("div");
    el.className = "space";
    const row = document.createElement("div");
    row.className = "row";
    span(row, "name", space.name);
    span(row, "id", "#" + space.id);
    span(row, "ownership " + space.ownership, space.ownership);
    if (space.windowShown) span(row, "window", "\u25A0 window open");
    if (space.agentTaskState) span(row, "state", space.agentTaskState);
    el.appendChild(row);
    if (space.tabs && space.tabs.length) {
      const tabs = document.createElement("div");
      tabs.className = "tabs";
      for (const tab of space.tabs) {
        const line = document.createElement("div");
        if (tab.active) line.className = "active";
        line.textContent = tab.title || tab.url || "(untitled)";
        tabs.appendChild(line);
      }
      el.appendChild(tabs);
    }
    const actions = document.createElement("div");
    actions.className = "actions";
    for (const [kind, label] of [["view", "View window"],
                                 ["handoff", "Hand off to user"],
                                 ["takeover", "Take over (agent)"],
                                 ["close", "Close"]]) {
      const btn = document.createElement("button");
      btn.textContent = label;
      btn.addEventListener("click", () => action(space.id, kind));
      actions.appendChild(btn);
    }
    el.appendChild(actions);
    list.appendChild(el);
  }
}
function refresh() {
  chrome.send("querySpaces");
}
refresh();
setInterval(refresh, 2000);)JS";

std::string SpacesJson() {
  base::ListValue spaces;
  for (const auto& space : SpaceManager::GetInstance()->List()) {
    base::DictValue entry;
    entry.Set("id", space.id);
    entry.Set("name", space.name);
    entry.Set("taskId", space.task_id);
    entry.Set("createdBy",
              space.created_by == SpaceManager::Owner::kAgent ? "agent"
                                                              : "user");
    entry.Set("ownership",
              space.ownership == SpaceManager::Ownership::kAgent
                  ? "agent"
              : space.ownership == SpaceManager::Ownership::kUser
                  ? "user"
                  : "agentDelegatedToUser");
    entry.Set("agentTaskState", space.agent_task_state);
    entry.Set("windowShown", space.window_shown);
    base::ListValue tabs;
    for (const auto& tab : space.tabs) {
      base::DictValue t;
      t.Set("title", tab.title);
      t.Set("url", tab.url);
      t.Set("active", tab.active);
      tabs.Append(std::move(t));
    }
    entry.Set("tabs", std::move(tabs));
    spaces.Append(std::move(entry));
  }
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(spaces)), &json);
  return json;
}

}  // namespace

PrismSpacesUIConfig::PrismSpacesUIConfig()
    : content::DefaultWebUIConfig<PrismSpacesUI>(content::kChromeUIScheme,
                                                 kPrismSpacesHost) {
  // This config is constructed by RegisterChromeWebUIConfigs during browser
  // startup — a guaranteed-early point to inject the chrome-side delegate.
  SetSpaceWindowDelegate(GetPrismSpaceWindowDelegate());
}
PrismSpacesUIConfig::~PrismSpacesUIConfig() = default;

PrismSpacesUI::PrismSpacesUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  auto* browser_context = web_ui->GetWebContents()->GetBrowserContext();
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      browser_context, kPrismSpacesHost);
  // Data flows via chrome.send/CallJavascriptFunction; the script is a
  // subresource (app.js) so the default 'self' CSP applies unchanged.
  source->SetRequestFilter(
      base::BindRepeating([](const std::string& path) { return true; }),
      base::BindRepeating(
          [](const std::string& path,
             content::WebUIDataSource::GotDataCallback callback) {
            if (path == "app.js") {
              std::move(callback).Run(
                  base::MakeRefCounted<base::RefCountedString>(kAppJs));
              return;
            }
            std::move(callback).Run(
                base::MakeRefCounted<base::RefCountedString>(kPageHtml));
          }));

  web_ui->RegisterMessageCallback(
      "querySpaces",
      base::BindRepeating(&PrismSpacesUI::OnQuerySpaces,
                          base::Unretained(this)));
  web_ui->RegisterMessageCallback(
      "spaceAction",
      base::BindRepeating(&PrismSpacesUI::OnAction, base::Unretained(this)));
}

PrismSpacesUI::~PrismSpacesUI() = default;

void PrismSpacesUI::OnQuerySpaces(const base::ListValue& args) {
  // The page is our own (served from the request filter above), so the
  // lifecycle concern behind CallJavascriptFunctionUnsafe's name does not
  // apply beyond startup — the page only queries after it is interactive.
  web_ui()->CallJavascriptFunctionUnsafe("prismSpaces.onData",
                                         base::ValueView(SpacesJson()));
}

void PrismSpacesUI::OnAction(const base::ListValue& args) {
  if (args.size() != 2 || !args[0].is_int() || !args[1].is_string()) {
    return;
  }
  const int id = args[0].GetInt();
  const std::string& action = args[1].GetString();
  SpaceManager* manager = SpaceManager::GetInstance();

  if (action == "view") {
    if (auto* delegate = GetSpaceWindowDelegate()) {
      // Tabs already live in the window when shown; windowless ones would be
      // handed over by the owning handler's Prism.showTaskSpace. From the
      // page there is nothing to move — the delegate just opens/focuses.
      delegate->ShowTaskSpace(id, {});
    }
  } else if (action == "handoff") {
    manager->HandOff(id);
  } else if (action == "takeover") {
    manager->TakeOver(id);
  } else if (action == "close") {
    // Windowed tabs stay with the user (they are plain user tabs once shown);
    // windowless tabs are destroyed through their agent hosts, which prunes
    // the owning handler's bookkeeping via the destroyed-WebContents hook.
    if (const auto* space = manager->Find(id)) {
      for (const auto& tab : space->tabs) {
        if (auto host = content::DevToolsAgentHost::GetForId(tab.target_id)) {
          host->Close();
        }
      }
    }
    manager->Close(id);
  }
}

}  // namespace prism
