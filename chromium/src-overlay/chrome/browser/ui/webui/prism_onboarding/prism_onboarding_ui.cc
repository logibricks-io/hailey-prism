// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/ui/webui/prism_onboarding/prism_onboarding_ui.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "base/compiler_specific.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/memory/ref_counted_memory.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/importer/external_process_importer_host.h"
#include "chrome/browser/importer/importer_list.h"
#include "chrome/browser/importer/profile_writer.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/prism/prism_chrome_importer.h"
#include "chrome/browser/prism/prism_chrome_live_import.h"
#include "chrome/browser/prism/prism_onboarding_actions.h"
#include "chrome/browser/profiles/profile.h"
#include "components/user_data_importer/common/importer_data_types.h"
#include "components/user_data_importer/common/importer_type.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"

// Binary assets (Kulim Park display fonts) live in assets/ next to this file;
// assets.gen.cc (checked in, regenerate via chromium/scripts/gen_webui_assets.py)
// turns them into a byte table served through the request filter below.
namespace PrismOnboarding {

struct PrismOnboardingAsset {
  const char* path;
  const unsigned char* data;
  size_t size;
};

const PrismOnboardingAsset* FindPrismOnboardingAsset(std::string_view path);

}  // namespace PrismOnboarding

namespace prism {

namespace {

// ------------------------------- the page -----------------------------------
//
// 4-step first-run flow per docs/ego-welcome-ui-recon.md §3: splash (rotating
// agent names) → pitch ("Feels like Chrome") → import wizard (drives the same
// staged+live Chrome import as chrome://prism-welcome) → finish (default
// browser / Dock / crash reports). Dark-to-light blue gradient, Kulim Park
// display font (OFL, bundled). Branding swaps per recon §6. No telemetry.
// Static markup + external app.js (WebUI CSP: no inline scripts, no runtime
// innerHTML — Trusted Types).
constexpr char kPageHtml[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Welcome to Prism</title>
<style>
  @font-face { font-family: "Kulim Park"; font-style: normal; font-weight: 200;
               font-display: swap;
               src: url("assets/KulimPark-ExtraLight.ttf") format("truetype"); }
  @font-face { font-family: "Kulim Park"; font-style: italic; font-weight: 200;
               font-display: swap;
               src: url("assets/KulimPark-ExtraLightItalic.ttf") format("truetype"); }
  @font-face { font-family: "Kulim Park"; font-style: normal; font-weight: 600;
               font-display: swap;
               src: url("assets/KulimPark-SemiBold.ttf") format("truetype"); }
  :root {
    --accent: #7eb3fe;
    --ink-1: #fffffff2; --ink-2: #ffffffc2; --ink-3: #ffffff8a;
    --panel: #ffffff12; --panel-line: #ffffff24;
  }
  * { box-sizing: border-box; }
  [hidden] { display: none !important; }
  html, body { height: 100%; }
  body {
    margin: 0; overflow: hidden; color: var(--ink-1);
    font-family: -apple-system, system-ui, sans-serif;
    background: linear-gradient(155deg, #0a1322 0%, #10234a 52%, #2a55b0 125%);
  }
  body::before { content: ""; position: fixed; inset: 0; pointer-events: none;
    background: radial-gradient(60% 50% at 78% 12%, #7eb3fe30, transparent 70%),
                radial-gradient(45% 40% at 12% 88%, #7eb3fe1c, transparent 70%); }

  .stage { position: fixed; inset: 0; }
  .step { position: absolute; inset: 0; display: flex; flex-direction: column;
          align-items: center; justify-content: center; padding: 48px;
          opacity: 0; transform: translateX(48px); pointer-events: none;
          transition: opacity .38s ease, transform .38s ease; }
  .step.current { opacity: 1; transform: none; pointer-events: auto; }
  .step.leaving { opacity: 0; transform: translateX(-48px); }

  .brandmark { display: inline-flex; align-items: center; gap: 12px; }
  .brandmark .word { font-family: "Kulim Park", -apple-system, sans-serif;
                     font-weight: 600; font-size: 26px; letter-spacing: .01em; }
  .corner-brand { position: absolute; top: 36px; left: 44px; }
  .corner-glyph { position: absolute; top: 28px; right: 36px; opacity: .5; }

  .splash-line { font-family: "Kulim Park", -apple-system, sans-serif;
                 font-weight: 200; font-size: 44px; line-height: 1.25;
                 text-align: center; max-width: 720px; margin: 0; }
  .splash-line .rot { color: var(--accent); font-style: italic;
                      display: inline-block; min-width: 220px; text-align: left; }
  .splash-sub { color: var(--ink-2); font-size: 17px; margin: 22px 0 0; }

  .btn { border: 0; cursor: pointer; border-radius: 999px; font-weight: 600;
         font-size: 16px; padding: 14px 34px; transition: filter .15s,
         transform .15s; font-family: inherit; }
  .btn.primary { background: var(--accent); color: #0a1322; }
  .btn.primary:hover { filter: brightness(1.08); transform: translateY(-1px); }
  .btn.ghost { background: transparent; color: var(--ink-2); }
  .btn.ghost:hover { color: var(--ink-1); }
  .btn-row { display: flex; align-items: center; gap: 10px; margin-top: 40px; }

  .dots { position: absolute; bottom: 34px; left: 0; right: 0; display: flex;
          justify-content: center; gap: 8px; }
  .dots span { width: 7px; height: 7px; border-radius: 50%;
               background: #ffffff3d; transition: background .2s; }
  .dots span.on { background: var(--accent); }

  h1.display { font-family: "Kulim Park", -apple-system, sans-serif;
               font-weight: 600; font-size: 52px; line-height: 1.08;
               text-align: center; margin: 0; max-width: 760px; }
  p.display-sub { color: var(--ink-2); font-size: 17px; text-align: center;
                  max-width: 560px; margin: 20px 0 0; line-height: 1.55; }

  /* pitch illustration: a plain window morphing into a Prism window */
  .morph { display: flex; align-items: center; gap: 34px; margin-top: 44px; }
  .win { width: 300px; border-radius: 14px; background: #0d1729e6;
         border: 1px solid var(--panel-line); overflow: hidden;
         box-shadow: 0 18px 50px #00000055; }
  .win .bar { display: flex; align-items: center; gap: 7px; padding: 10px 12px;
              background: #ffffff0a; }
  .win .bar i { width: 10px; height: 10px; border-radius: 50%;
                background: #ffffff2a; display: block; }
  .win .url { margin: 12px; height: 30px; border-radius: 999px;
              background: #ffffff10; display: flex; align-items: center;
              padding: 0 12px; gap: 8px; color: var(--ink-3); font-size: 12px; }
  .win .body { height: 108px; margin: 0 12px 12px; border-radius: 8px;
               background: #ffffff08; }
  .win.prism .url { background: #ffffff16; }
  .win.prism .chip { display: inline-flex; align-items: center; gap: 6px;
                     border-radius: 999px; background: #ffffff14;
                     padding: 3px 10px 3px 5px; color: var(--ink-1);
                     font-size: 11px; font-weight: 600; }
  .morph .arrow { color: var(--accent); flex-shrink: 0; }

  /* import wizard */
  .import-panel { width: 560px; max-width: 92vw; border-radius: 20px;
                  background: var(--panel); border: 1px solid var(--panel-line);
                  backdrop-filter: blur(24px); padding: 8px; margin-top: 36px; }
  .browser-row { display: flex; align-items: center; gap: 14px;
                 padding: 14px 16px; border-radius: 14px; }
  .browser-row + .browser-row { border-top: 1px solid #ffffff10; }
  .browser-row .grow { flex: 1; min-width: 0; }
  .browser-row .name { font-weight: 600; font-size: 15px; }
  .browser-row .meta { color: var(--ink-3); font-size: 12.5px; margin-top: 2px; }
  .profile-chip { display: inline-flex; align-items: center; gap: 6px;
                  color: var(--ink-2); font-size: 13px; border-radius: 999px;
                  border: 1px solid var(--panel-line); padding: 5px 12px; }
  .browser-icon { width: 30px; height: 30px; border-radius: 50%;
                  position: relative; flex-shrink: 0; }
  .browser-icon.chrome { background: conic-gradient(#ea4335 0 33%,
                          #fbbc05 33% 66%, #34a853 66% 100%); }
  .browser-icon.chrome::before { content: ""; position: absolute; inset: 3px;
                  border-radius: 50%; background: #fff; }
  .browser-icon.chrome::after { content: ""; position: absolute; inset: 8px;
                  border-radius: 50%; background: #4285f4; }
  .browser-icon.other { background: #ffffff1c; display: flex;
                  align-items: center; justify-content: center;
                  color: var(--ink-2); }
  .check { width: 22px; height: 22px; border-radius: 7px; cursor: pointer;
           border: 1.5px solid #ffffff55; background: transparent;
           display: inline-flex; align-items: center; justify-content: center;
           color: #0a1322; padding: 0; flex-shrink: 0; }
  .check.on { background: var(--accent); border-color: var(--accent); }
  .check svg { opacity: 0; }
  .check.on svg { opacity: 1; }
  .import-note { color: var(--ink-3); font-size: 12.5px; text-align: center;
                 margin: 14px 0 0; }
  .privacy { color: var(--ink-2); font-size: 13px; margin-top: 26px;
             display: flex; align-items: center; gap: 8px; }
  .import-status { color: var(--accent); font-size: 13.5px; margin-top: 16px;
                   min-height: 1.2em; }

  /* finish */
  .finish-check { width: 84px; height: 84px; border-radius: 50%;
                  background: #7eb3fe26; border: 1.5px solid var(--accent);
                  display: flex; align-items: center; justify-content: center;
                  color: var(--accent); margin-bottom: 30px; }
  .options { display: flex; flex-direction: column; gap: 14px; margin-top: 34px; }
  .options label { display: flex; align-items: center; gap: 12px;
                   font-size: 15px; color: var(--ink-1); cursor: pointer; }
</style></head><body>
<div class="stage" id="stage">

  <section class="step current" id="step-splash">
    <div class="corner-brand brandmark">
      <svg width="30" height="30" viewBox="0 0 576 576" fill="none" aria-hidden="true"><rect x="0" y="0" width="158" height="158" rx="21" fill="#FBFBF9"/><rect x="0" y="209" width="158" height="158" rx="21" fill="#FBFBF9"/><rect x="0" y="418" width="158" height="158" rx="21" fill="#FBFBF9"/><rect x="209" y="418" width="158" height="158" rx="21" fill="#FBFBF9"/><rect x="418" y="418" width="158" height="158" rx="21" fill="#FBFBF9"/><circle cx="497" cy="79" r="71" fill="#C87858"/></svg>
      <span class="word">Prism</span>
    </div>
    <div class="corner-glyph" aria-hidden="true">
      <svg width="180" height="180" viewBox="0 0 576 576" fill="none"><rect x="0" y="0" width="158" height="158" rx="21" stroke="#ffffff59"/><rect x="0" y="209" width="158" height="158" rx="21" stroke="#ffffff59"/><rect x="0" y="418" width="158" height="158" rx="21" stroke="#ffffff59"/><rect x="209" y="418" width="158" height="158" rx="21" stroke="#ffffff59"/><rect x="418" y="418" width="158" height="158" rx="21" stroke="#ffffff59"/><circle cx="497" cy="79" r="71" stroke="#C87858" stroke-opacity=".6"/></svg>
    </div>
    <p class="splash-line">the browser you can share with<br>
      <span class="rot" id="rotator">Claude Code</span></p>
    <p class="splash-sub">Your agents drive. You stay in control.</p>
    <div class="btn-row">
      <button type="button" class="btn primary" id="getStarted">Get started &#8594;</button>
    </div>
  </section>

  <section class="step" id="step-pitch">
    <h1 class="display">Feels like Chrome.<br>Nothing to relearn.</h1>
    <p class="display-sub">Prism keeps everything familiar, while helping you get things done better.</p>
    <div class="morph" aria-hidden="true">
      <div class="win">
        <div class="bar"><i></i><i></i><i></i></div>
        <div class="url">example.com</div>
        <div class="body"></div>
      </div>
      <span class="arrow"><svg width="44" height="44" viewBox="0 0 24 24" fill="none"><path d="M4 12h15M13 6l6 6-6 6" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg></span>
      <div class="win prism">
        <div class="bar"><i></i><i></i><i></i></div>
        <div class="url"><span class="chip"><svg width="12" height="12" viewBox="0 0 576 576" fill="none"><rect x="0" y="0" width="158" height="158" rx="21" fill="#FBFBF9"/><rect x="0" y="209" width="158" height="158" rx="21" fill="#FBFBF9"/><rect x="0" y="418" width="158" height="158" rx="21" fill="#FBFBF9"/><rect x="209" y="418" width="158" height="158" rx="21" fill="#FBFBF9"/><rect x="418" y="418" width="158" height="158" rx="21" fill="#FBFBF9"/><circle cx="497" cy="79" r="71" fill="#C87858"/></svg>Prism</span><span style="margin-left:4px">example.com</span></div>
        <div class="body"></div>
      </div>
    </div>
    <div class="btn-row">
      <button type="button" class="btn primary" id="pitchContinue">Continue</button>
    </div>
  </section>

  <section class="step" id="step-import">
    <h1 class="display">Import from another browser</h1>
    <p class="display-sub">Bring your bookmarks, history, and more into Prism, so it's ready to work for you from day one.</p>
    <div class="import-panel" id="importPanel">
      <div class="browser-row" id="chromeRow" hidden>
        <span class="browser-icon chrome" aria-hidden="true"></span>
        <div class="grow">
          <div class="name">Chrome</div>
          <div class="meta">Bookmarks, history, cookies, passwords, preferences</div>
        </div>
        <span class="profile-chip" id="chromeProfileChip">Default profile</span>
        <button type="button" class="check on" id="chromeCheck" aria-label="Import from Chrome" aria-pressed="true"><svg width="13" height="13" viewBox="0 0 24 24" fill="none"><path d="M4 12l5 5 11-11" stroke="currentColor" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"/></svg></button>
      </div>
    </div>
    <p class="import-note" id="importNote" hidden>Each selected browser profile will be imported separately</p>
    <p class="import-status" id="importStatus"></p>
    <p class="privacy">&#x1F512; Your data stays on your device. We never collect it.</p>
    <div class="btn-row">
      <button type="button" class="btn primary" id="importBtn">Import</button>
      <button type="button" class="btn ghost" id="skipImport">Skip</button>
    </div>
  </section>

  <section class="step" id="step-finish">
    <div class="finish-check"><svg width="38" height="38" viewBox="0 0 24 24" fill="none"><path d="M4 12.5l5 5L20 6.5" stroke="currentColor" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round"/></svg></div>
    <h1 class="display">You're all set!</h1>
    <p class="display-sub" id="finishSub">Prism is ready when you are.</p>
    <div class="options">
      <label><button type="button" class="check on" id="optDefault" aria-pressed="true"><svg width="13" height="13" viewBox="0 0 24 24" fill="none"><path d="M4 12l5 5 11-11" stroke="currentColor" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"/></svg></button>Set as default browser</label>
      <label><button type="button" class="check on" id="optDock" aria-pressed="true"><svg width="13" height="13" viewBox="0 0 24 24" fill="none"><path d="M4 12l5 5 11-11" stroke="currentColor" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"/></svg></button>Add to Dock</label>
      <label><button type="button" class="check" id="optCrash" aria-pressed="false"><svg width="13" height="13" viewBox="0 0 24 24" fill="none"><path d="M4 12l5 5 11-11" stroke="currentColor" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"/></svg></button>Share crash reports</label>
    </div>
    <div class="btn-row">
      <button type="button" class="btn primary" id="openPrism">Open Prism</button>
    </div>
  </section>

  <div class="dots" id="dots" aria-hidden="true">
    <span class="on"></span><span></span><span></span><span></span>
  </div>
</div>
<script src="app.js"></script>
</body></html>)HTML";

constexpr char kAppJs[] = R"JS(// chrome://prism-onboarding step machine + import bridge (vanilla; no inline
// handlers, no runtime innerHTML — Trusted Types are enforced on WebUI).

// ---- steps ----
const STEPS = ["step-splash", "step-pitch", "step-import", "step-finish"];
let stepIndex = 0;
function goTo(next) {
  const cur = document.getElementById(STEPS[stepIndex]);
  const dst = document.getElementById(STEPS[next]);
  cur.classList.add("leaving");
  cur.classList.remove("current");
  setTimeout(() => cur.classList.remove("leaving"), 400);
  dst.classList.add("current");
  stepIndex = next;
  const dots = document.getElementById("dots").children;
  for (let i = 0; i < dots.length; i++) dots[i].classList.toggle("on", i === next);
}

// ---- splash rotator (1.5s, like the original) ----
const AGENT_NAMES = ["Claude Code", "Codex", "OpenCode", "Cursor", "Hermes", "Kiro", "OpenClaw"];
let rotIndex = 0;
setInterval(() => {
  rotIndex = (rotIndex + 1) % AGENT_NAMES.length;
  document.getElementById("rotator").textContent = AGENT_NAMES[rotIndex];
}, 1500);

// ---- checkboxes ----
function wireCheck(id) {
  const el = document.getElementById(id);
  el.addEventListener("click", (e) => {
    e.preventDefault();
    e.stopPropagation();
    const on = el.classList.toggle("on");
    el.setAttribute("aria-pressed", String(on));
  });
  return () => el.classList.contains("on");
}
const chromeChecked = wireCheck("chromeCheck");
const wantDefault = wireCheck("optDefault");
const wantDock = wireCheck("optDock");
const wantCrash = wireCheck("optCrash");

// ---- state from C++ ----
let stagedForRestart = false;
const otherSelected = new Map();  // importer index -> checkbox getter
window.prismOnboarding = {
  onState(stateJson) {
    const state = JSON.parse(stateJson);
    if (state.chromeFound) {
      document.getElementById("chromeRow").hidden = false;
      document.getElementById("importNote").hidden = false;
    }
    const panel = document.getElementById("importPanel");
    for (const other of state.otherBrowsers) {
      const row = document.createElement("div");
      row.className = "browser-row";
      const icon = document.createElement("span");
      icon.className = "browser-icon other";
      icon.textContent = "\u{1F310}";
      const grow = document.createElement("div");
      grow.className = "grow";
      const name = document.createElement("div");
      name.className = "name";
      name.textContent = other.name;
      const meta = document.createElement("div");
      meta.className = "meta";
      meta.textContent = "Bookmarks, history, passwords";
      grow.appendChild(name);
      grow.appendChild(meta);
      const check = document.createElement("button");
      check.type = "button";
      check.className = "check on";
      check.setAttribute("aria-pressed", "true");
      check.setAttribute("aria-label", "Import from " + other.name);
      const tick = document.createElementNS("http://www.w3.org/2000/svg", "svg");
      tick.setAttribute("width", "13"); tick.setAttribute("height", "13");
      tick.setAttribute("viewBox", "0 0 24 24"); tick.setAttribute("fill", "none");
      const path = document.createElementNS("http://www.w3.org/2000/svg", "path");
      path.setAttribute("d", "M4 12l5 5 11-11");
      path.setAttribute("stroke", "currentColor");
      path.setAttribute("stroke-width", "3");
      path.setAttribute("stroke-linecap", "round");
      path.setAttribute("stroke-linejoin", "round");
      tick.appendChild(path);
      check.appendChild(tick);
      row.appendChild(icon);
      row.appendChild(grow);
      row.appendChild(check);
      panel.appendChild(row);
      check.addEventListener("click", () => {
        const on = check.classList.toggle("on");
        check.setAttribute("aria-pressed", String(on));
      });
      otherSelected.set(other.index, check);
      document.getElementById("importNote").hidden = false;
    }
    if (state.chromeFound || state.otherBrowsers.length) return;
    // Nothing to import: jump straight past the wizard when reached.
    document.getElementById("importStatus").textContent =
      "No other browser data found on this machine.";
    document.getElementById("importBtn").hidden = true;
  },
  onImportStatus(text) {
    document.getElementById("importStatus").textContent = text;
  },
  onImportReport(reportJson) {
    const report = JSON.parse(reportJson);
    stagedForRestart = !!report.stagedForRestart;
    if (stagedForRestart) {
      document.getElementById("finishSub").textContent =
        "Restart to bring your Chrome data along.";
      document.getElementById("openPrism").textContent = "Restart & open Prism";
    }
    goTo(3);
  },
  onFinished() {
    window.location.href = "chrome://newtab/";
  },
};

// ---- navigation ----
document.getElementById("getStarted").addEventListener("click", () => goTo(1));
document.getElementById("pitchContinue").addEventListener("click", () => {
  goTo(2);
  chrome.send("onboardingGetState");
});
document.getElementById("skipImport").addEventListener("click", () => goTo(3));
document.getElementById("importBtn").addEventListener("click", () => {
  const others = [];
  for (const [index, check] of otherSelected) {
    if (check.classList.contains("on")) others.push(index);
  }
  const importChrome =
    !document.getElementById("chromeRow").hidden && chromeChecked();
  if (!importChrome && others.length === 0) { goTo(3); return; }
  document.getElementById("importBtn").hidden = true;
  document.getElementById("skipImport").hidden = true;
  chrome.send("onboardingImport", [importChrome, others]);
});
document.getElementById("openPrism").addEventListener("click", () => {
  chrome.send("onboardingFinish",
    [wantDefault(), wantDock(), wantCrash(), stagedForRestart]);
  // If a restart is required the browser relaunches itself; otherwise C++
  // answers with prismOnboarding.onFinished.
});
)JS";

// Serves the page/app.js/assets through the WebUI request filter.
void OnDataRequest(const std::string& path,
                   content::WebUIDataSource::GotDataCallback callback) {
  if (path == "app.js") {
    std::move(callback).Run(
        base::MakeRefCounted<base::RefCountedString>(kAppJs));
    return;
  }
  if (path.starts_with("assets/")) {
    if (const auto* asset =
            ::PrismOnboarding::FindPrismOnboardingAsset(path.substr(7))) {
      std::move(callback).Run(
          base::MakeRefCounted<base::RefCountedStaticMemory>(
              UNSAFE_BUFFERS(base::span<const uint8_t>(asset->data,
                                                       asset->size))));
      return;
    }
  }
  std::move(callback).Run(
      base::MakeRefCounted<base::RefCountedString>(kPageHtml));
}

}  // namespace

PrismOnboardingUIConfig::PrismOnboardingUIConfig()
    : content::DefaultWebUIConfig<PrismOnboardingUI>(content::kChromeUIScheme,
                                                     "prism-onboarding") {}
PrismOnboardingUIConfig::~PrismOnboardingUIConfig() = default;

PrismOnboardingUI::PrismOnboardingUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  importer_list_ = std::make_unique<ImporterList>();
  auto* browser_context = web_ui->GetWebContents()->GetBrowserContext();
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      browser_context, "prism-onboarding");
  // Bundled display fonts load from the same origin.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::FontSrc, "font-src 'self';");
  source->SetRequestFilter(
      base::BindRepeating([](const std::string& path) { return true; }),
      base::BindRepeating(&OnDataRequest));
  web_ui->RegisterMessageCallback(
      "onboardingGetState",
      base::BindRepeating(&PrismOnboardingUI::OnGetState,
                          weak_factory_.GetWeakPtr()));
  web_ui->RegisterMessageCallback(
      "onboardingImport",
      base::BindRepeating(&PrismOnboardingUI::OnImport,
                          weak_factory_.GetWeakPtr()));
  web_ui->RegisterMessageCallback(
      "onboardingFinish",
      base::BindRepeating(&PrismOnboardingUI::OnFinish,
                          weak_factory_.GetWeakPtr()));
}

PrismOnboardingUI::~PrismOnboardingUI() = default;

void PrismOnboardingUI::ReportImportStatus(const std::string& status) {
  web_ui()->CallJavascriptFunctionUnsafe("prismOnboarding.onImportStatus",
                                         base::ValueView(status));
}

void PrismOnboardingUI::OnGetState(const base::ListValue& args) {
  chrome_found_ =
      !ChromeSourceProfileDir().empty() &&
      base::PathExists(ChromeSourceProfileDir());
  importer_list_->DetectSourceProfiles(
      "en-US", /*include_interactive_profiles=*/true,
      base::BindOnce(&PrismOnboardingUI::OnSourceProfilesDetected,
                     weak_factory_.GetWeakPtr()));
}

void PrismOnboardingUI::OnSourceProfilesDetected() {
  state_detection_done_ = true;
  MaybeSendState();
}

void PrismOnboardingUI::MaybeSendState() {
  base::DictValue state;
  state.Set("chromeFound", chrome_found_);
  base::ListValue others;
  if (state_detection_done_) {
    for (size_t i = 0; i < importer_list_->count(); ++i) {
      const auto& source = importer_list_->GetSourceProfileAt(i);
      // Browsers only — a bookmarks-HTML file source is not a wizard row.
      if (source.importer_type != user_data_importer::TYPE_SAFARI &&
          source.importer_type != user_data_importer::TYPE_FIREFOX) {
        continue;
      }
      base::DictValue entry;
      entry.Set("name", base::UTF16ToUTF8(source.importer_name));
      entry.Set("index", base::checked_cast<int>(i));
      others.Append(std::move(entry));
    }
  }
  state.Set("otherBrowsers", std::move(others));
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(state)), &json);
  web_ui()->CallJavascriptFunctionUnsafe("prismOnboarding.onState",
                                         base::ValueView(std::move(json)));
}

void PrismOnboardingUI::OnImport(const base::ListValue& args) {
  Profile* profile =
      Profile::FromBrowserContext(web_ui()->GetWebContents()->GetBrowserContext());
  const bool import_chrome = args.size() > 0 && args[0].GetBool();
  const base::ListValue* others =
      args.size() > 1 ? args[1].GetIfList() : nullptr;

  // Stock importer framework lane (Safari / Firefox).
  if (others && state_detection_done_) {
    for (const base::Value& v : *others) {
      const int index = v.GetInt();
      if (index < 0 ||
          static_cast<size_t>(index) >= importer_list_->count()) {
        continue;
      }
      const auto& source = importer_list_->GetSourceProfileAt(index);
      // Matches the settings-page call site's ownership idiom.
      auto* host = new ExternalProcessImporterHost();
      host->StartImportSettings(source, profile, user_data_importer::ALL,
                                new ProfileWriter(profile));
    }
  }

  if (!import_chrome) {
    base::DictValue report;
    report.Set("chromeFound", false);
    report.Set("stagedForRestart", false);
    std::string json;
    base::JSONWriter::Write(base::Value(std::move(report)), &json);
    web_ui()->CallJavascriptFunctionUnsafe("prismOnboarding.onImportReport",
                                           base::ValueView(std::move(json)));
    return;
  }

  // Chrome lane: staged full import + live bookmarks/history merge (same
  // machinery as chrome://prism-welcome; see prism_chrome_live_import.*).
  ReportImportStatus("Importing from Chrome… (macOS may ask once for keychain access)");
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&RunFullChromeImport, profile->GetPath()),
      base::BindOnce(
          [](base::WeakPtr<PrismOnboardingUI> self, Profile* profile,
             ChromeImportResult result) {
            if (!self) {
              return;
            }
            base::DictValue report = std::move(result.report);
            if (result.live.chrome_found) {
              report = MergeChromeImportLiveData(profile, result);
            }
            self->staged_for_restart_ =
                report.FindBool("stagedForRestart").value_or(false);
            std::string json;
            base::JSONWriter::Write(base::Value(std::move(report)), &json);
            self->web_ui()->CallJavascriptFunctionUnsafe(
                "prismOnboarding.onImportReport",
                base::ValueView(std::move(json)));
          },
          weak_factory_.GetWeakPtr(), profile));
}

void PrismOnboardingUI::OnFinish(const base::ListValue& args) {
  const bool set_default = args.size() > 0 && args[0].GetBool();
  const bool add_to_dock = args.size() > 1 && args[1].GetBool();
  const bool crash_reports = args.size() > 2 && args[2].GetBool();
  const bool restart = args.size() > 3 && args[3].GetBool();

  if (set_default) {
    SetPrismAsDefaultBrowser();
  }
  if (add_to_dock) {
    AddPrismToDock();
  }
  if (crash_reports) {
    SetCrashReportsEnabled(true);
  }
  MarkOnboardingCompleted();

  if (restart) {
    // The staged import lands during the next startup, before the profile
    // opens those files (ApplyStagedChromeImport).
    chrome::AttemptRestart();
    return;
  }
  web_ui()->CallJavascriptFunctionUnsafe("prismOnboarding.onFinished",
                                         base::ValueView());
}

}  // namespace prism
