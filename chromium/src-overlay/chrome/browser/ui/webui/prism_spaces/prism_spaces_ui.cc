// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/ui/webui/prism_spaces/prism_spaces_ui.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/ref_counted_memory.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "chrome/browser/prism/prism_space_window_delegate.h"
#include "chrome/browser/prism/prism_spaces_ui_constants.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/devtools_agent_host_client.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "prism/browser/spaces/space_manager.h"

namespace prism {

namespace {

// The spaces overview: a Mission-Control-style card wall. Plain HTML/JS (no
// framework, no grit — served from a request filter to keep the patch surface
// minimal). Data comes from chrome.send("querySpaces") pushes; actions go
// through chrome.send("spaceAction"); thumbnails are captured on demand from
// the space's active tab and served from thumb/<id>.png.
constexpr char kPageHtml[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><title>Prism Spaces</title>
<style>
  /* Design tokens mirror chrome://prism-welcome (dark + blue accent). */
  :root {
    color-scheme: dark;
    --bg-1: #1f1f1f; --bg-3: #171717; --fg-1: #2e2e2e; --fg-3: #424242;
    --text-1: #ffffffe6; --text-2: #ffffffb3; --text-3: #ffffff7a;
    --text-4: #ffffff3d;
    --accent: #7eb3fe;
  }
  * { box-sizing: border-box; }
  [hidden] { display: none !important; }
  body { font-family: -apple-system, system-ui, sans-serif;
         background: var(--bg-3); color: var(--text-1);
         margin: 0; padding: 22px 32px 88px; animation: page-in .25s ease; }
  @keyframes page-in { from { opacity: 0; } }
  @keyframes card-in { from { opacity: 0; transform: translateY(10px)
                       scale(.97); } }

  header { display: flex; align-items: center; margin-bottom: 22px; }
  header .brand { display: inline-flex; align-items: center; gap: 8px;
                  color: var(--text-2); font-weight: 600; font-size: 14px; }
  header .side { flex: 1; }
  /* recon §4: "N Space(s) ⌄" sits top center */
  #spacesCaption { position: relative; margin: 0 auto; }
  #captionBtn { display: inline-flex; align-items: center; gap: 7px;
                background: none; border: 0; color: var(--text-1);
                font-size: 15px; font-weight: 600; cursor: pointer;
                padding: 6px 12px; border-radius: 999px; }
  #captionBtn:hover { background: #ffffff0f; }
  #captionBtn .chev { color: var(--text-3); font-size: 11px; }
  #captionMenu { position: absolute; top: calc(100% + 6px); left: 50%;
                 transform: translateX(-50%); min-width: 220px; z-index: 30;
                 background: var(--fg-1); border: 1px solid #ffffff14;
                 border-radius: 14px; padding: 6px; display: none;
                 box-shadow: 0 10px 36px #00000070; }
  #captionMenu.open { display: block; }
  #captionMenu button { display: flex; width: 100%; align-items: center;
                        gap: 10px; border: 0; background: none;
                        color: var(--text-1); font-size: 13.5px;
                        padding: 8px 10px; border-radius: 9px;
                        cursor: pointer; text-align: left; }
  #captionMenu button:hover { background: #ffffff10; }
  #captionMenu .sub { margin-left: auto; color: var(--text-3);
                      font-size: 11.5px; }

  #wall { display: grid; grid-template-columns: repeat(auto-fill,
          minmax(300px, 1fr)); gap: 18px; }
  .card { background: var(--bg-1); border: 1px solid #ffffff0d;
          border-radius: 16px; overflow: hidden; cursor: pointer;
          transition: border-color .15s, box-shadow .15s, transform .15s;
          animation: card-in .28s cubic-bezier(.2,.9,.3,1.2) backwards;
          animation-delay: calc(var(--i, 0) * 45ms); }
  .card:hover { border-color: #ffffff26; transform: translateY(-2px); }
  /* recon §4: selected space gets a blue border */
  .card.focused { border-color: var(--accent);
                  box-shadow: 0 0 0 1.5px var(--accent); }
  .thumb { position: relative; aspect-ratio: 16 / 10; background: #101010;
           display: flex; align-items: center; justify-content: center; }
  .thumb img { position: absolute; inset: 0; width: 100%; height: 100%;
               object-fit: cover; object-position: top; }
  .thumb .initial { font-size: 42px; font-weight: 700; color: #ffffff2e; }
  /* recon §4: left-bottom "Space" label, right-bottom muted watermark */
  .thumb .space-label { position: absolute; left: 10px; bottom: 10px;
           font-size: 11.5px; font-weight: 600; color: var(--text-1);
           background: #0000008c; border-radius: 999px; padding: 3px 10px;
           backdrop-filter: blur(8px); }
  .thumb .watermark { position: absolute; right: 10px; bottom: 10px;
           display: inline-flex; align-items: center; gap: 5px;
           color: var(--text-3); font-size: 11px; opacity: .8; }
  .meta { padding: 12px 14px 14px; }
  .row { display: flex; align-items: center; gap: 8px; flex-wrap: wrap; }
  .name { font-size: 14.5px; font-weight: 600; }
  .chip { font-size: 11px; padding: 2px 8px; border-radius: 999px;
          border: 1px solid #ffffff1f; color: var(--text-3); }
  .chip.agent { color: #8fd3a5; border-color: #2f5d43; }
  .chip.agentDelegatedToUser { color: #ffb86b; border-color: #6b4a2a; }
  .chip.focused { color: var(--accent); border-color: var(--accent); }
  .chip.running { color: #8fd3a5; border-color: #2f5d43; }
  .state { margin-top: 6px; color: var(--text-2); font-size: 12.5px;
           min-height: 15px; }
  .state:empty::before { content: "\2014"; color: var(--text-4); }
  .actions { margin-top: 10px; display: flex; gap: 8px; }
  button { background: #ffffff0f; color: var(--text-1); border: 0;
           border-radius: 8px; padding: 5px 12px; font-size: 12.5px;
           cursor: pointer; font-family: inherit; }
  button:hover { background: #ffffff1a; }
  button.danger:hover { background: #5d2f3d; }
  /* recon §4: the "+" create card */
  .newspace { display: flex; align-items: center; justify-content: center;
              gap: 8px; min-height: 220px; color: var(--text-3);
              font-size: 14px; border: 1.5px dashed #ffffff21;
              background: transparent; }
  .newspace:hover { color: var(--text-1); border-color: var(--accent); }
  .newspace .plus { font-size: 26px; color: var(--accent); }
  .empty { color: var(--text-3); }

  /* recon §4: first-run ⌥S hint bar */
  #hintBar { position: fixed; left: 50%; bottom: 22px;
             transform: translateX(-50%); display: flex; align-items: center;
             gap: 12px; background: var(--fg-1); border: 1px solid #ffffff1a;
             border-radius: 999px; padding: 10px 12px 10px 18px;
             color: var(--text-2); font-size: 13px; z-index: 40;
             box-shadow: 0 10px 34px #00000066;
             animation: card-in .3s .3s cubic-bezier(.2,.9,.3,1.2) backwards; }
  #hintBar kbd { color: var(--text-1); font-family: inherit; font-weight: 600; }
  #hintBar button { border-radius: 50%; padding: 3px 8px;
                    color: var(--text-3); }
</style></head><body>
<header>
  <span class="brand"><svg width="18" height="18" viewBox="0 0 576 576" fill="none" aria-hidden="true"><rect x="0" y="0" width="158" height="158" rx="21" fill="#FBFBF9"/><rect x="0" y="209" width="158" height="158" rx="21" fill="#FBFBF9"/><rect x="0" y="418" width="158" height="158" rx="21" fill="#FBFBF9"/><rect x="209" y="418" width="158" height="158" rx="21" fill="#FBFBF9"/><rect x="418" y="418" width="158" height="158" rx="21" fill="#FBFBF9"/><circle cx="497" cy="79" r="71" fill="#C87858"/></svg>Prism</span>
  <span class="side"></span>
  <div id="spacesCaption">
    <button type="button" id="captionBtn" aria-haspopup="true" aria-expanded="false">
      <span id="captionText">Spaces</span><span class="chev">&#9662;</span>
    </button>
    <div id="captionMenu" role="menu"></div>
  </div>
  <span class="side" style="text-align:right"><button id="deleteAll" class="danger">Delete all</button></span>
</header>
<div id="wall"><div class="empty">Loading&hellip;</div></div>
<div id="hintBar" hidden>
  <span>Hold <kbd>&#x2325;</kbd> and press <kbd>S</kbd> repeatedly to quick-switch Spaces.</span>
  <button type="button" id="hintDismiss" aria-label="Dismiss hint">&#10005;</button>
</div>
<script src="app.js"></script></body></html>)HTML";

constexpr char kAppJs[] = R"JS(let refreshCounter = 0;
function action(id, kind) {
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
function ownershipLabel(space) {
  if (space.ownership === "agent") return "Agent";
  if (space.ownership === "agentDelegatedToUser") return "Delegated to you";
  return "Yours";
}

// ---- top-center "N Space(s) ⌄" caption + dropdown (recon §4) ----
const captionBtn = document.getElementById("captionBtn");
const captionMenu = document.getElementById("captionMenu");
captionBtn.addEventListener("click", (e) => {
  e.stopPropagation();
  const open = captionMenu.classList.toggle("open");
  captionBtn.setAttribute("aria-expanded", String(open));
});
document.addEventListener("click", () => captionMenu.classList.remove("open"));

function buildCaptionMenu(spaces, focused) {
  captionMenu.replaceChildren();
  for (const space of spaces) {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.setAttribute("role", "menuitem");
    span(btn, "", space.name || "Space");
    const sub = space.id === focused ? "Focused"
              : space.ownership === "agent" ? "Agent" : "";
    if (sub) span(btn, "sub", sub);
    btn.addEventListener("click", (e) => {
      e.stopPropagation();
      captionMenu.classList.remove("open");
      action(space.id, "focus");
    });
    captionMenu.appendChild(btn);
  }
}

// ---- first-run ⌥S hint bar (dismissal persists per profile) ----
const hintBar = document.getElementById("hintBar");
try {
  if (!localStorage.getItem("prismSpacesHintDismissed")) hintBar.hidden = false;
} catch (e) { /* storage unavailable: show the hint */ hintBar.hidden = false; }
document.getElementById("hintDismiss").addEventListener("click", () => {
  hintBar.hidden = true;
  try { localStorage.setItem("prismSpacesHintDismissed", "1"); } catch (e) {}
});

function onData(payloadJson) {
  const data = JSON.parse(payloadJson);
  const spaces = data.spaces || [];
  document.getElementById("captionText").textContent =
      spaces.length + " Space" + (spaces.length === 1 ? "" : "s");
  buildCaptionMenu(spaces, data.focused);

  const wall = document.getElementById("wall");
  wall.replaceChildren();
  let i = 0;
  for (const space of spaces) {
    const card = document.createElement("div");
    card.className = "card" + (space.id === data.focused ? " focused" : "");
    card.style.setProperty("--i", i++);
    card.dataset.space = space.id;
    card.addEventListener("click", () => action(space.id, "focus"));

    const thumb = document.createElement("div");
    thumb.className = "thumb";
    const initial = document.createElement("div");
    initial.className = "initial";
    initial.textContent = (space.name || "?").trim().charAt(0).toUpperCase();
    thumb.appendChild(initial);
    if (space.hasTabs) {
      const img = document.createElement("img");
      img.src = "thumb/" + space.id + "." + refreshCounter + ".png";
      img.alt = "";
      thumb.appendChild(img);
    }
    // recon §4: left-bottom "Space" label + right-bottom muted watermark
    const label = document.createElement("span");
    label.className = "space-label";
    label.textContent = "Space";
    thumb.appendChild(label);
    const wm = document.createElement("span");
    wm.className = "watermark";
    wm.textContent = "Your Prism";
    thumb.appendChild(wm);
    card.appendChild(thumb);

    const meta = document.createElement("div");
    meta.className = "meta";
    const row = document.createElement("div");
    row.className = "row";
    span(row, "name", space.name);
    span(row, "chip " + space.ownership, ownershipLabel(space));
    if (space.id === data.focused) span(row, "chip focused", "Focused");
    if (space.windowShown) span(row, "chip", "Window open");
    if (space.ownership === "agent" && space.agentTaskState)
      span(row, "chip running", "Running");
    meta.appendChild(row);
    const state = document.createElement("div");
    state.className = "state";
    state.textContent = space.agentTaskState || "";
    meta.appendChild(state);

    const actions = document.createElement("div");
    actions.className = "actions";
    const specs = [["focus", "Focus"]];
    if (space.ownership === "agent") specs.push(["handoff", "Hand off"]);
    if (space.ownership === "agentDelegatedToUser")
      specs.push(["takeover", "Return to agent"]);
    specs.push(["close", "Close"]);
    for (const [kind, label2] of specs) {
      const btn = document.createElement("button");
      btn.textContent = label2;
      if (kind === "close") btn.className = "danger";
      btn.addEventListener("click", (e) => {
        e.stopPropagation();
        action(space.id, kind);
      });
      actions.appendChild(btn);
    }
    meta.appendChild(actions);
    card.appendChild(meta);
    wall.appendChild(card);
  }

  const add = document.createElement("div");
  add.className = "card newspace";
  add.style.setProperty("--i", i);
  const plus = document.createElement("span");
  plus.className = "plus";
  plus.textContent = "+";
  add.appendChild(plus);
  add.appendChild(document.createTextNode(" Create a new Space"));
  add.addEventListener("click", () => action(0, "create"));
  wall.appendChild(add);
}
document.getElementById("deleteAll").addEventListener("click", () => {
  action(0, "deleteAll");
});
function refresh() {
  chrome.send("querySpaces");
}
refresh();
setInterval(() => { refreshCounter++; refresh(); }, 2000);)JS";

// 1x1 transparent PNG, served when a thumbnail cannot be captured (space
// without tabs, closed target, capture failure). The card paints a styled
// placeholder behind the image, so a transparent pixel reads as "empty".
const uint8_t kFallbackPng[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
    0x0a, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x62, 0x00, 0x01, 0x00, 0x00,
    0x05, 0x00, 0x01, 0x0d, 0x0a, 0x2d, 0xb4, 0x00, 0x00, 0x00, 0x00, 0x49,
    0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

struct ThumbCacheEntry {
  std::string bytes;
  base::TimeTicks at;
};

std::map<int, ThumbCacheEntry>& ThumbCache() {
  static base::NoDestructor<std::map<int, ThumbCacheEntry>> cache;
  return *cache;
}

void RespondPng(content::WebUIDataSource::GotDataCallback callback,
                const std::string& png) {
  std::vector<uint8_t> bytes(png.begin(), png.end());
  std::move(callback).Run(
      base::MakeRefCounted<base::RefCountedBytes>(std::move(bytes)));
}

// One-shot screenshot of a space tab via the internal DevTools protocol
// (attach → Page.captureScreenshot → detach), after SnapshotJob's pattern.
// Self-owned: deletes itself when the callback has run.
class ThumbnailJob : public content::DevToolsAgentHostClient {
 public:
  ThumbnailJob(int space_id,
               scoped_refptr<content::DevToolsAgentHost> host,
               content::WebUIDataSource::GotDataCallback callback)
      : space_id_(space_id),
        host_(std::move(host)),
        callback_(std::move(callback)) {}
  ~ThumbnailJob() override = default;

  void Run() {
    watchdog_.Start(
        FROM_HERE, base::Seconds(5),
        base::BindOnce(&ThumbnailJob::Finish, base::Unretained(this),
                       std::string()));
    host_->AttachClient(this);
    // 1280x800 is the fixed viewport of windowless tabs; scale 0.25 → 320px.
    const std::string message =
        R"({"id":1,"method":"Page.captureScreenshot","params":{"format":"png","clip":{"x":0,"y":0,"width":1280,"height":800,"scale":0.25}}})";
    host_->DispatchProtocolMessage(this, base::as_byte_span(message));
  }

  // content::DevToolsAgentHostClient:
  void DispatchProtocolMessage(content::DevToolsAgentHost* agent_host,
                               base::span<const uint8_t> message) override {
    std::string_view raw(reinterpret_cast<const char*>(message.data()),
                         message.size());
    auto parsed = base::JSONReader::Read(raw, base::JSON_PARSE_RFC);
    if (!parsed || !parsed->is_dict() ||
        parsed->GetDict().FindInt("id") != 1) {
      return;  // protocol event or unrelated response
    }
    const std::string* data = parsed->GetDict().FindStringByDottedPath(
        "result.data");
    std::string png;
    if (data) {
      if (auto decoded = base::Base64Decode(*data)) {
        png.assign(decoded->begin(), decoded->end());
      }
    }
    Finish(std::move(png));
  }
  void AgentHostClosed(content::DevToolsAgentHost* agent_host) override {
    Finish(std::string());
  }
  std::string GetTypeForMetrics() override { return "Other"; }

 private:
  void Finish(std::string png) {
    watchdog_.Stop();
    if (host_) {
      host_->DetachClient(this);
      host_ = nullptr;
    }
    if (!png.empty()) {
      ThumbCache()[space_id_] = {png, base::TimeTicks::Now()};
    } else {
      png.assign(reinterpret_cast<const char*>(kFallbackPng),
                 sizeof(kFallbackPng));
    }
    if (callback_) {
      std::move(callback_).Run(
          base::MakeRefCounted<base::RefCountedBytes>(
              std::vector<uint8_t>(png.begin(), png.end())));
    }
    delete this;  // self-owned; last statement, touches nothing afterwards
  }

  const int space_id_;
  scoped_refptr<content::DevToolsAgentHost> host_;
  content::WebUIDataSource::GotDataCallback callback_;
  base::OneShotTimer watchdog_;
};

void ServeThumbnail(const std::string& path,
                    content::WebUIDataSource::GotDataCallback callback) {
  // Paths are "thumb/<id>.<cache-buster>.png"; the id is the digit run.
  int space_id = 0;
  const std::string rest = path.substr(6);  // strlen("thumb/")
  for (char c : rest) {
    if (c < '0' || c > '9') {
      break;
    }
    space_id = space_id * 10 + (c - '0');
  }
  if (space_id <= 0) {
    RespondPng(std::move(callback),
               std::string(reinterpret_cast<const char*>(kFallbackPng),
                           sizeof(kFallbackPng)));
    return;
  }

  auto& cache = ThumbCache();
  auto it = cache.find(space_id);
  if (it != cache.end() &&
      base::TimeTicks::Now() - it->second.at < base::Seconds(1)) {
    RespondPng(std::move(callback), it->second.bytes);
    return;
  }

  // Capture the space's active tab (the last one marked active).
  std::string target_id;
  if (const auto* space = SpaceManager::GetInstance()->Find(space_id)) {
    for (const auto& tab : space->tabs) {
      if (tab.active) {
        target_id = tab.target_id;
      }
    }
    if (target_id.empty() && !space->tabs.empty()) {
      target_id = space->tabs.back().target_id;
    }
  }
  auto host = target_id.empty()
                  ? nullptr
                  : content::DevToolsAgentHost::GetForId(target_id);
  if (!host) {
    RespondPng(std::move(callback),
               std::string(reinterpret_cast<const char*>(kFallbackPng),
                           sizeof(kFallbackPng)));
    return;
  }

  auto* job = new ThumbnailJob(space_id, std::move(host), std::move(callback));
  job->Run();
}

std::string SpacesJson() {
  auto* manager = SpaceManager::GetInstance();
  base::DictValue root;
  root.Set("focused", manager->focused_space_id());
  base::ListValue spaces;
  for (const auto& space : manager->List()) {
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
    entry.Set("hasTabs", !space.tabs.empty());
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
  root.Set("spaces", std::move(spaces));
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(root)), &json);
  return json;
}

void CloseSpaceTabsAndSpace(SpaceManager* manager, int id) {
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
            if (path.starts_with("thumb/")) {
              ServeThumbnail(path, std::move(callback));
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

  if (action == "create") {
    // User-created space: agents may select but not drive it until claimed.
    manager->Create("", SpaceManager::Owner::kUser);
  } else if (action == "deleteAll") {
    for (const auto& space : manager->List()) {
      CloseSpaceTabsAndSpace(manager, space.id);
    }
  } else if (action == "view" || action == "focus") {
    if (auto* delegate = GetSpaceWindowDelegate()) {
      // Tabs already live in the window when shown; windowless ones would be
      // handed over by the owning handler's Prism.showTaskSpace. From the
      // page there is nothing to move — the delegate just opens/focuses.
      manager->set_focused_space_id(id);
      delegate->ShowTaskSpace(id, {});
      manager->SetWindowShown(id, true);
    }
  } else if (action == "handoff") {
    manager->HandOff(id);
  } else if (action == "takeover") {
    manager->TakeOver(id);
  } else if (action == "close") {
    CloseSpaceTabsAndSpace(manager, id);
  }

  // Push the mutated state immediately instead of waiting for the poll.
  web_ui()->CallJavascriptFunctionUnsafe("prismSpaces.onData",
                                         base::ValueView(SpacesJson()));
}

}  // namespace prism
