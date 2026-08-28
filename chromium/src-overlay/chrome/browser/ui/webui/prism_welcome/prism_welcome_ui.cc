// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/ui/webui/prism_welcome/prism_welcome_ui.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/compiler_specific.h"
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
#include "chrome/browser/profiles/profile.h"
#include "components/user_data_importer/common/importer_data_types.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "content/public/common/url_constants.h"

// Binary assets live in assets/ next to this file; assets.gen.cc (checked in,
// regenerate via chromium/scripts/gen_webui_assets.py) turns them into a
// byte table served through the request filter below. The generator emits the
// table in a global namespace, so the matching declaration lives at file
// scope here (identical struct layout on both sides).
namespace PrismWelcome {

struct PrismWelcomeAsset {
  const char* path;
  const unsigned char* data;
  size_t size;
};

const PrismWelcomeAsset* FindPrismWelcomeAsset(std::string_view path);

}  // namespace PrismWelcome

namespace prism {

namespace {

// ------------------------------- the page -----------------------------------
//
// A 1:1 rebuild of ego lite's ego://welcome ("Welcome aboard!") — dark theme,
// glyph header, 64px heading, three guide cards with container-query scaling
// — using the extracted reference assets (docs/ego-welcome-ui-recon.md).
// Branding swaps per recon §6: LogiBricks mark, "Prism" strings,
// /prism-browser prompt, no telemetry. Static markup + an external app.js
// (WebUI CSP: no inline scripts, no runtime innerHTML — Trusted Types).
constexpr char kPageHtml[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Welcome to Prism</title>
<style>
  :root {
    --bg-1:#1f1f1f; --bg-3:#171717; --fg-1:#2e2e2e; --fg-3:#424242;
    --fg-4:#4d4d4d; --fg-5:#575757;
    --text-1:#ffffffe6; --text-2:#ffffffb3; --text-3:#ffffff7a;
    --text-4:#ffffff3d; --text-5:#ffffff29;
    --bg-alpha-1:#ffffff05; --bg-alpha-2:#ffffff0a; --bg-alpha-3:#ffffff0f;
    --bg-alpha-4:#ffffff14; --fg-alpha-4:#2e2e2e66;
    --accent:#7eb3fe;
  }
  * { box-sizing: border-box; }
  [hidden] { display: none !important; }
  body { background: var(--bg-1); margin: 0;
         font-family: -apple-system, system-ui, sans-serif;
         color: var(--text-1); }
  main { height: 100dvh; width: 100%; overflow-y: auto; padding: 0 16px;
         scrollbar-gutter: stable both-edges; display: flex; flex-direction: column; }
  .column { margin: auto; width: 100%; max-width: 1440px; flex-shrink: 0;
            display: flex; flex-direction: column; align-items: center; }
  header.brand { display: flex; flex-direction: column; align-items: center;
                 gap: 48px; padding-top: 48px; flex-shrink: 0; }
  header.brand .titles { display: flex; flex-direction: column;
                         align-items: center; gap: 20px; text-align: center; }
  h1 { font-weight: 900; color: var(--text-2); font-size: 64px;
       line-height: 0.85; margin: 0; letter-spacing: -0.01em; }
  .subtitle { max-width: 360px; color: var(--text-3); font-size: 16px;
              margin: 0; }
  .gap-spacer, .bottom-spacer { width: 100%; flex-shrink: 0; }

  .cards-viewport { container-type: inline-size; display: flex;
                    width: 100%; flex-shrink: 0; justify-content: center; }
  .cards-stage { width: 100%; max-width: 1440px; }
  .cards-row { display: flex; flex-wrap: wrap; align-items: center;
               justify-content: center; gap: 12px; }
  @media (min-width: 1264px) {
    .cards-stage { --cards-scale: min(1, calc(100cqw / 1440px));
                   position: relative; width: min(100%, 1440px);
                   max-width: none; aspect-ratio: 1440 / 480; }
    .cards-row { position: absolute; top: 0; left: 50%; width: 1440px;
                 flex-wrap: nowrap;
                 transform: translateX(-50%) scale(var(--cards-scale));
                 transform-origin: top center; }
  }
  @container (min-width: 1440px) {
    .cards-stage { position: static; width: 100%; max-width: 1440px;
                   aspect-ratio: auto; }
    .cards-row { position: static; width: 100%; transform: none; }
  }

  .card { position: relative; height: 480px; width: 472px; flex-shrink: 0;
          overflow: hidden; border-radius: 32px; background: var(--bg-3);
          corner-shape: superellipse(1.4); }
  .glow { pointer-events: none; position: absolute; inset: 0;
          border-radius: inherit; opacity: 0; corner-shape: superellipse(1.4);
          box-shadow: inset 0 0 6px 0 var(--accent),
                      inset 0 0 32px 0 var(--accent);
          transition: opacity .15s; }
  .group:hover .glow, .card:hover .glow { opacity: 1; }

  .card header { display: flex; flex-direction: column; padding: 32px 40px; }
  .card h2 { font-weight: 600; color: var(--text-1); font-size: 28px;
             line-height: 1.15; margin: 0; letter-spacing: -0.01em; }
  .card .sub { color: var(--text-3); font-size: 16px; line-height: 1.19;
               margin: 0; }

  /* Card A — ready to work */
  #card-ready .arcs { pointer-events: none; position: absolute;
                      bottom: -92px; left: -50px; height: 353px; width: 575px;
                      max-width: none; }
  #card-ready .inner { position: relative; display: flex; height: 100%;
                       flex-direction: column; }
  #card-ready .inner header { gap: 12px; }
  .tipcard { margin-top: auto; padding: 8px; }
  .tipcard > div { display: flex; flex-direction: column;
                   border-radius: 24px; border: 1px solid var(--bg-alpha-2);
                   background: var(--fg-alpha-4); padding: 16px 20px;
                   backdrop-filter: blur(20px); corner-shape: superellipse(1.4); }
  .tipcard ul { margin: 0; padding-left: 24px; list-style: disc;
                color: var(--text-3); font-size: 16px; line-height: 1.19; }
  .tipcard li { margin: 10px 0; }
  .tipcard li:first-child { margin-top: 0; }
  .tipcard li:last-child { margin-bottom: 0; }
  .tipcard strong { color: var(--text-1); font-weight: 500; }
  .tipcard code { font-family: ui-monospace, monospace; font-size: 13px; }

  /* Card B — try with your agent */
  #card-try { display: flex; flex-direction: column; }
  #card-try header { gap: 12px; padding: 32px 32px 24px; }
  #card-try h2 { display: flex; flex-wrap: wrap; align-items: center; }
  .agent-spin { position: relative; margin: 0 6px; width: 20px; height: 20px;
                flex-shrink: 0; display: inline-block; }
  .agent-spin img { position: absolute; inset: 0; width: 100%; height: 100%;
                    opacity: 0; }
  .agent-spin img.on { opacity: 1; }
  .openin { position: relative; display: flex; width: 100%; align-items: center;
            justify-content: space-between; gap: 12px; border-radius: 999px;
            background: var(--fg-1); padding: 8px 12px 8px 16px;
            corner-shape: superellipse(1.4); }
  .openin:hover { background: var(--fg-3); }
  .openin .left { display: flex; align-items: center; gap: 12px;
                  pointer-events: none; position: relative; z-index: 1; }
  .openin .label { font-weight: 500; color: var(--text-1); font-size: 16px; }
  .agent-menu-btn { pointer-events: auto; display: flex; align-items: center;
                    gap: 6px; border-radius: 999px; background: var(--bg-alpha-2);
                    padding: 8px 6px 8px 8px; border: 0; cursor: pointer;
                    color: var(--text-1); corner-shape: superellipse(1.4); }
  .agent-menu-btn:hover { background: var(--bg-alpha-3); }
  .agent-menu-btn img { width: 20px; height: 20px; }
  .agent-menu-btn .name { font-weight: 500; font-size: 14px; }
  .agent-menu-btn .chev { color: var(--text-3); display: inline-flex; }
  .agent-menu { position: absolute; top: calc(100% + 6px); left: 60px; z-index: 20;
                min-width: 200px; border-radius: 16px; background: var(--fg-2, #363636);
                box-shadow: 0 8px 32px 4px #00000066; padding: 6px; display: none; }
  .agent-menu.open { display: block; }
  .agent-menu button { display: flex; align-items: center; gap: 10px;
                       width: 100%; border: 0; background: none; color: var(--text-1);
                       font-size: 14px; font-weight: 500; padding: 8px 10px;
                       border-radius: 10px; cursor: pointer; text-align: left; }
  .agent-menu button:hover { background: var(--bg-alpha-3); }
  .agent-menu button img { width: 20px; height: 20px; }
  .openin .arrow { display: flex; width: 32px; height: 32px; flex-shrink: 0;
                   align-items: center; justify-content: center; padding: 6px;
                   color: var(--text-1); position: relative; z-index: 1; }
  .paste-hint { flex-shrink: 0; white-space: nowrap; font-weight: 500;
                font-size: 16px; position: relative; z-index: 1; }
  .paste-hint .kbd { color: var(--text-4); }
  .divider { display: flex; align-items: center; justify-content: center;
             gap: 6px; padding: 0 8px; margin-top: 12px; }
  .divider .line { height: 0; flex: 1; border-top: 1px dashed var(--fg-5); }
  .divider span { flex-shrink: 0; font-weight: 500; color: var(--text-4);
                  font-size: 12px; }
  .promptrow { display: flex; align-items: center; justify-content: center;
               gap: 10px; padding: 8px 8px 0; margin: 0 32px; }
  .promptrow p { min-width: 0; flex: 1; font-weight: 500; color: var(--text-3);
                 font-size: 12px; line-height: 14px; margin: 0; }
  .promptrow p .cmd { color: var(--text-3); }
  .copybtn { display: flex; flex-shrink: 0; align-items: center;
             border-radius: 8px; padding: 6px; border: 0; cursor: pointer;
             color: var(--text-1); background: none;
             transition: background .15s; corner-shape: superellipse(1.4); }
  .copybtn:hover, .copybtn.copied { background: var(--bg-alpha-4); }

  .termwrap { margin-top: auto; display: flex; height: 200px;
              align-items: flex-start; justify-content: center;
              overflow: hidden; padding: 8px 8px 0; }
  .termclip { height: 228px; width: 418px; flex-shrink: 0; overflow: hidden;
              border-radius: 12px; corner-shape: superellipse(1.4); }
  .terminal { display: flex; height: 363px; width: 665px; flex-direction: column;
              overflow: hidden; background: var(--bg-1); padding-top: 20px;
              padding-left: 13px; font-family: ui-monospace, "SF Mono", monospace;
              color: var(--text-2); font-size: 14px; line-height: 1.15;
              transform: scale(0.72); transform-origin: top left; }
  .terminal .thead { display: flex; align-items: center; gap: 18px; }
  .terminal .thead img { height: 42px; width: 69px; flex-shrink: 0; }
  .terminal .thead .names { display: flex; flex-direction: column; gap: 1px; }
  .terminal .thead .names p { display: flex; gap: 3px; white-space: nowrap; margin: 0; }
  .terminal .thead .names .bold { font-weight: 700; color: var(--text-1); }
  .terminal .thead .names .dim { color: var(--text-3); }
  .terminal .thead .names > span { color: var(--text-3); }
  .terminal .prompt { margin-top: 22px; align-self: flex-start;
                      background: var(--bg-alpha-4); padding: 2px; }
  .terminal .prompt p { white-space: nowrap; font-weight: 500; margin: 0; }
  .terminal .prompt .chev { color: var(--text-5); }
  .terminal .prompt .cmd { color: var(--text-1); }
  .terminal .reply { margin-top: 20px; display: flex; align-items: flex-start;
                     gap: 6px; font-weight: 500; }
  .terminal .reply .dot { margin-top: 5px; width: 6px; height: 6px;
                          flex-shrink: 0; border-radius: 999px;
                          background: var(--text-2); }
  .terminal .reply .body p { white-space: nowrap; margin: 0; color: var(--text-5); }
  .terminal .reply .body .hi { color: var(--text-3); }
  .terminal .reply .body .hi2 { color: var(--text-4); }
  .terminal .skel { margin-top: 16px; display: flex; flex-direction: column;
                    gap: 16px; }
  .terminal .skel div { height: 18px; background: var(--bg-alpha-1); }
  .terminal .skel .a { width: 524px; }
  .terminal .skel .b { width: 402px; }

  /* Card C — discover */
  #card-discover .links { position: absolute; left: 0; right: 0; bottom: 0;
                          display: grid; grid-template-columns: 1fr 1fr;
                          gap: 8px; padding: 8px; }
  .mini { position: relative; display: flex; height: 140px;
          flex-direction: column; justify-content: flex-end;
          overflow: hidden; border-radius: 24px; background: var(--fg-1);
          padding: 10px 12px; text-decoration: none; color: inherit;
          corner-shape: superellipse(1.4); }
  .mini.wide { grid-column: span 2; flex-direction: row;
               align-items: center; justify-content: space-between; }
  .mini .bgimg { pointer-events: none; position: absolute; inset: 0;
                 width: 100%; height: 100%; object-fit: cover;
                 object-position: bottom right; }
  .chip { position: relative; display: inline-flex; width: fit-content;
          align-items: center; justify-content: center; border-radius: 999px;
          background: var(--fg-3); padding: 4px 6px; font-weight: 500;
          color: var(--text-3); font-size: 12px; line-height: 1.17; }
  .mini .docstack { pointer-events: none; position: absolute; top: 20px;
                    left: 50%; transform: translateX(-50%); width: 188px;
                    display: flex; flex-direction: column; gap: 9px; }
  .mini .docstack .t { font-weight: 700; color: var(--text-1); font-size: 18px;
                       line-height: 1.17; margin: 0; }
  .mini .docstack .s1 { font-weight: 500; color: var(--text-3); font-size: 6px;
                        line-height: 1; margin: 0; }
  .mini .docstack .s2 { font-weight: 500; color: var(--text-2); font-size: 5px;
                        line-height: 1; margin: 0; }
  .mini .docstack img { width: 100%; border-radius: 2px; }
  .usecases { position: relative; display: flex; flex-shrink: 0;
              flex-direction: column; gap: 16px; padding: 24px 0 0 20px; }
  .usecases .icons { display: flex; align-items: flex-end; gap: 12px; }
  .usecases .icons img { height: 42px; width: auto; }
  .usecases ul { margin: 0; padding: 0; display: flex; flex-direction: column;
                 gap: 3px; font-family: Georgia, "Times New Roman", serif;
                 color: var(--text-1); font-size: 12px; line-height: 1.5;
                 list-style: none; }
  .usecases .fade { pointer-events: none; position: absolute; left: 0; right: 0;
                    bottom: 0; height: 60px;
                    background: linear-gradient(to bottom, transparent, var(--fg-1)); }
  .learnmore { margin-right: 46px; display: flex; flex-direction: column;
               align-items: center; justify-content: center; }
  .learnmore .circ { color: var(--text-3); display: inline-flex; }
  .learnmore .txt { font-weight: 500; color: var(--text-2); font-size: 12px; }
</style></head><body>
<main id="main"><div class="column">
  <header class="brand">
    <span role="img" aria-label="Prism" class="mark"><svg width="48" height="48" viewBox="0 0 576 576" fill="none" aria-hidden="true"><rect x="0" y="0" width="158" height="158" rx="21" fill="#FBFBF9"/><rect x="0" y="209" width="158" height="158" rx="21" fill="#FBFBF9"/><rect x="0" y="418" width="158" height="158" rx="21" fill="#FBFBF9"/><rect x="209" y="418" width="158" height="158" rx="21" fill="#FBFBF9"/><rect x="418" y="418" width="158" height="158" rx="21" fill="#FBFBF9"/><circle cx="497" cy="79" r="71" fill="#C87858"/></svg></span>
    <div class="titles">
      <h1>Welcome<br>aboard!</h1>
      <p class="subtitle">Follow the guides below to get started</p>
    </div>
  </header>
  <div aria-hidden="true" class="gap-spacer" id="gapSpacer"></div>
  <div class="cards-viewport" id="cardsViewport"><div class="cards-stage"><div class="cards-row">

    <section class="card" id="card-ready">
      <img class="arcs" id="arcsImg" alt="" aria-hidden="true">
      <div class="inner">
        <header>
          <h2>Prism is ready to<br>work with your agents</h2>
          <p class="sub">Your agent now runs browser tasks faster, reuses past successes, and uses fewer tokens.</p>
        </header>
        <div class="tipcard"><div>
          <ul>
            <li>To start using Prism, <strong>restart your agent.</strong></li>
            <li>If Prism is unavailable or the skill is missing, manually install it from <strong>github.com/logibricks-io/hailey-prism</strong>.</li>
          </ul>
        </div></div>
      </div>
      <div class="glow" aria-hidden="true"></div>
    </section>

    <section class="card" id="card-try">
      <header>
        <h2><span>Try Prism with your</span> <span class="agent-spin" id="agentSpin"></span> <span>agent</span></h2>
        <p class="sub">Type /prism-browser followed by your task in the agent, or specify using prism browser in your prompt.</p>
      </header>
      <div style="display:flex;flex-direction:column;padding:0 32px;">
        <div class="openin" id="openIn">
          <div class="left">
            <span class="label">Open in</span>
            <button type="button" class="agent-menu-btn" id="agentMenuBtn" aria-haspopup="listbox" aria-expanded="false">
              <img id="agentIcon" alt="">
              <span class="name" id="agentName">Codex</span>
              <span class="chev"><svg width="20" height="20" viewBox="0 0 24 24" fill="none"><path d="M7 10l5 5 5-5" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg></span>
            </button>
            <div class="agent-menu" id="agentMenu" role="listbox"></div>
          </div>
          <span class="paste-hint" id="pasteHint" hidden><span style="color:var(--text-1)">and Paste</span> <span class="kbd">&#x2318;V</span></span>
          <span class="arrow"><svg width="32" height="32" viewBox="0 0 24 24" fill="none"><path d="M5 12h14M13 6l6 6-6 6" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg></span>
          <button type="button" id="openInOverlay" aria-label="Open prompt" style="position:absolute;inset:0;border-radius:999px;background:none;border:0;cursor:pointer;corner-shape:superellipse(1.4);"></button>
        </div>
        <div class="divider"><span class="line"></span><span>or copy and paste in your terminal</span><span class="line"></span></div>
        <div class="promptrow">
          <p><span class="cmd">/prism-browser </span><span>OpenAI &amp; Anthropic blogs, summarize latest noteworthy updates</span></p>
          <button type="button" class="copybtn" id="copyBtn" aria-label="Copy prompt">
            <svg id="copyIcon" width="20" height="20" viewBox="0 0 24 24" fill="none"><rect x="9" y="9" width="12" height="12" rx="2" stroke="currentColor" stroke-width="2"/><path d="M5 15V5a2 2 0 0 1 2-2h10" stroke="currentColor" stroke-width="2" stroke-linecap="round"/></svg>
            <svg id="copiedIcon" width="20" height="20" viewBox="0 0 24 24" fill="none" hidden><path d="M4 12l5 5 11-11" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg>
          </button>
        </div>
      </div>
      <div class="termwrap"><div class="termclip"><div class="terminal">
        <div class="thead">
          <img src="assets/agent-opencode.png" alt="" aria-hidden="true">
          <div class="names">
            <p><span class="bold">Claude Code</span> <span class="dim">v2.1.159</span></p>
            <span>Opus 4.8</span>
          </div>
        </div>
        <div class="prompt"><p><span class="chev">&#x276F; </span><span class="cmd">/prism-browser summarize OpenAI &amp; Anthropic blogs</span></p></div>
        <div class="reply">
          <span aria-hidden="true" class="dot"></span>
          <div class="body">
            <p>I'll use prism-browser to explore OpenAI's <span class="hi">main</span> <span class="hi2">page </span><span>and discover</span></p>
            <p>today's interesting articles — featured article, "Did you know", "In</p>
            <p>the news", "On this day", etc.</p>
          </div>
        </div>
        <div class="skel"><div class="a"></div><div class="b"></div></div>
      </div></div></div>
      <div class="glow" aria-hidden="true"></div>
    </section>

    <section class="card" id="card-discover">
      <header><h2>Discover how Prism can enhance your life</h2></header>
      <div class="links">
        <a class="mini group" href="https://github.com/logibricks-io/hailey-prism#readme" target="_blank" rel="noopener noreferrer">
          <img class="bgimg" src="assets/tutorial.png" alt="">
          <span class="chip">Tutorial</span>
          <div class="glow" aria-hidden="true"></div>
        </a>
        <a class="mini group" href="https://github.com/logibricks-io/hailey-prism#readme" target="_blank" rel="noopener noreferrer">
          <div class="docstack">
            <p class="t">Quick start</p>
            <p class="s1">Two minutes to get your Codex, Claude agent working in the browser for you.</p>
            <p class="s2">Prism is a browser built for both you and your agents — based on Chromium, so your extensions,</p>
          </div>
          <span class="chip">Docs</span>
          <div class="glow" aria-hidden="true"></div>
        </a>
        <a class="mini wide group" href="https://github.com/logibricks-io/hailey-prism#readme" target="_blank" rel="noopener noreferrer">
          <div class="usecases">
            <div class="icons">
              <img src="assets/icon-research.png" alt="">
              <img src="assets/icon-career.png" alt="">
              <img src="assets/icon-house.png" alt="">
              <img src="assets/icon-finance.png" alt="">
            </div>
            <ul>
              <li>Analyze Competitors' Activity</li>
              <li>Find Yourself the Best Job</li>
              <li>Track Your Stocks in One Pass</li>
            </ul>
            <div class="fade"></div>
          </div>
          <div class="learnmore">
            <span class="circ"><svg width="64" height="64" viewBox="0 0 24 24" fill="none"><circle cx="12" cy="12" r="10" stroke="currentColor" stroke-width="1.5"/><path d="M15 9l-6 6M15 15V9H9" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/></svg></span>
            <span class="txt">Learn more</span>
          </div>
          <div class="glow" aria-hidden="true"></div>
        </a>
      </div>
      <div class="glow" aria-hidden="true"></div>
    </section>

  </div></div></div>
  <div aria-hidden="true" class="bottom-spacer" id="bottomSpacer"></div>
</div></main>
<script src="app.js"></script>
</body></html>)HTML";

constexpr char kAppJs[] = R"JS(// chrome://prism-welcome interactions (vanilla; no inline handlers, no
// runtime innerHTML — Trusted Types are enforced on WebUI).

// SVGs must be data URIs: the request filter serves bytes with a generic
// MIME type and <img> does not sniff SVG. (Raster assets are fine — they are
// sniffed.)
const ASSETS = {
  ARCS: "data:image/svg+xml,%3Csvg%20preserveAspectRatio%3D%27none%27%20width%3D%27100%25%27%20height%3D%27100%25%27%20overflow%3D%27visible%27%20style%3D%27display%3A%20block%3B%27%20viewBox%3D%270%200%20574.552%20353.008%27%20fill%3D%27none%27%20xmlns%3D%27http%3A//www.w3.org/2000/svg%27%3E%3Cg%20id%3D%27Group%203%27%3E%3Cpath%20id%3D%273%20%28Stroke%29%27%20d%3D%27M501.769%20176.504C501.769%20128.766%20506.723%2086.133%20514.427%2055.9424C518.312%2040.7161%20522.658%2029.6217%20526.805%2022.7476C527.883%2020.9605%20528.792%2019.6986%20529.513%2018.8155C530.233%2019.6986%20531.143%2020.9605%20532.221%2022.7476C536.368%2029.6217%20540.713%2040.7161%20544.599%2055.9424C552.303%2086.133%20557.257%20128.766%20557.257%20176.504C557.257%20224.242%20552.303%20266.875%20544.599%20297.065C540.713%20312.292%20536.368%20323.386%20532.221%20330.26C531.143%20332.047%20530.233%20333.308%20529.513%20334.192C528.792%20333.308%20527.883%20332.047%20526.805%20330.26C522.658%20323.386%20518.312%20312.292%20514.427%20297.065C506.723%20266.875%20501.769%20224.242%20501.769%20176.504ZM484.477%20178.785C484.789%20275.215%20504.833%20353.008%20529.513%20353.008L529.804%20353.005C554.544%20352.391%20574.552%20273.604%20574.552%20176.504C574.552%2079.0235%20554.387%200%20529.513%200C504.638%201.16444e-05%20484.474%2079.0235%20484.474%20176.504L484.477%20178.785Z%27%20fill%3D%27var%28--fill-0%2C%20%23F892B8%29%27/%3E%3Cpath%20id%3D%271%20%28Stroke%29%27%20d%3D%27M17.2212%20176.504C17.2214%2087.9752%2086.3648%2017.2316%20170.418%2017.2316C254.471%2017.2316%20323.615%2087.9752%20323.615%20176.504C323.615%20265.033%20254.471%20335.777%20170.418%20335.777V353.008L171.52%20353.005C265.132%20352.391%20340.836%20273.604%20340.836%20176.504C340.836%2079.0235%20264.537%200%20170.418%200L169.316%200.00350578C75.7038%200.617612%200.000176%2079.4043%205.6386e-05%20176.504L0.00356004%20177.645C0.596296%20274.601%2076.6663%20353.008%20170.418%20353.008V335.777C86.3647%20335.777%2017.2212%20265.033%2017.2212%20176.504Z%27%20fill%3D%27var%28--fill-0%2C%20%23009376%29%27/%3E%3Cpath%20id%3D%272%20%28Stroke%29%27%20d%3D%27M292.35%20176.504C292.35%20129.818%20301.696%2088.4025%20316.014%2059.2981C330.881%2029.0772%20348.165%2017.2316%20361.936%2017.2316C375.706%2017.2317%20392.99%2029.0774%20407.857%2059.2981C422.175%2088.4025%20431.521%20129.818%20431.521%20176.504C431.521%20223.189%20422.175%20264.606%20407.857%20293.71C392.99%20323.931%20375.706%20335.777%20361.936%20335.777V353.008L362.497%20353.005C410.194%20352.391%20448.767%20273.604%20448.767%20176.504C448.767%2079.0236%20409.891%200.000247006%20361.936%200C313.98%200%20275.103%2079.0235%20275.103%20176.504L275.11%20178.785C275.711%20275.215%20314.354%20353.008%20361.936%20353.008V335.777C348.165%20335.777%20330.881%20323.931%20316.014%20293.71C301.696%20264.606%20292.35%20223.189%20292.35%20176.504Z%27%20fill%3D%27var%28--fill-0%2C%20%23BEDB00%29%27/%3E%3C/g%3E%3C/svg%3E",
  AGENT_CLAUDE_CODE: "data:image/svg+xml,%3Csvg%20height%3D%271em%27%20style%3D%27flex%3Anone%3Bline-height%3A1%27%20viewBox%3D%270%200%2024%2024%27%20width%3D%271em%27%20xmlns%3D%27http%3A//www.w3.org/2000/svg%27%3E%3Ctitle%3EClaude%3C/title%3E%3Cpath%20d%3D%27M4.709%2015.955l4.72-2.647.08-.23-.08-.128H9.2l-.79-.048-2.698-.073-2.339-.097-2.266-.122-.571-.121L0%2011.784l.055-.352.48-.321.686.06%201.52.103%202.278.158%201.652.097%202.449.255h.389l.055-.157-.134-.098-.103-.097-2.358-1.596-2.552-1.688-1.336-.972-.724-.491-.364-.462-.158-1.008.656-.722.881.06.225.061.893.686%201.908%201.476%202.491%201.833.365.304.145-.103.019-.073-.164-.274-1.355-2.446-1.446-2.49-.644-1.032-.17-.619a2.97%202.97%200%2001-.104-.729L6.283.134%206.696%200l.996.134.42.364.62%201.414%201.002%202.229%201.555%203.03.456.898.243.832.091.255h.158V9.01l.128-1.706.237-2.095.23-2.695.08-.76.376-.91.747-.492.584.28.48.685-.067.444-.286%201.851-.559%202.903-.364%201.942h.212l.243-.242.985-1.306%201.652-2.064.73-.82.85-.904.547-.431h1.033l.76%201.129-.34%201.166-1.064%201.347-.881%201.142-1.264%201.7-.79%201.36.073.11.188-.02%202.856-.606%201.543-.28%201.841-.315.833.388.091.395-.328.807-1.969.486-2.309.462-3.439.813-.042.03.049.061%201.549.146.662.036h1.622l3.02.225.79.522.474.638-.079.485-1.215.62-1.64-.389-3.829-.91-1.312-.329h-.182v.11l1.093%201.068%202.006%201.81%202.509%202.33.127.578-.322.455-.34-.049-2.205-1.657-.851-.747-1.926-1.62h-.128v.17l.444.649%202.345%203.521.122%201.08-.17.353-.608.213-.668-.122-1.374-1.925-1.415-2.167-1.143-1.943-.14.08-.674%207.254-.316.37-.729.28-.607-.461-.322-.747.322-1.476.389-1.924.315-1.53.286-1.9.17-.632-.012-.042-.14.018-1.434%201.967-2.18%202.945-1.726%201.845-.414.164-.717-.37.067-.662.401-.589%202.388-3.036%201.44-1.882.93-1.086-.006-.158h-.055L4.132%2018.56l-1.13.146-.487-.456.061-.746.231-.243%201.908-1.312-.006.006z%27%20fill%3D%27%23D97757%27%20fill-rule%3D%27nonzero%27%3E%3C/path%3E%3C/svg%3E",
  AGENT_CODEX: "data:image/svg+xml,%3Csvg%20height%3D%271em%27%20style%3D%27flex%3Anone%3Bline-height%3A1%27%20viewBox%3D%270%200%2024%2024%27%20width%3D%271em%27%20xmlns%3D%27http%3A//www.w3.org/2000/svg%27%3E%3Ctitle%3ECodex%3C/title%3E%3Cpath%20d%3D%27M19.503%200H4.496A4.496%204.496%200%20000%204.496v15.007A4.496%204.496%200%20004.496%2024h15.007A4.496%204.496%200%200024%2019.503V4.496A4.496%204.496%200%200019.503%200z%27%20fill%3D%27%23fff%27%3E%3C/path%3E%3Cpath%20d%3D%27M9.064%203.344a4.578%204.578%200%20012.285-.312c1%20.115%201.891.54%202.673%201.275.01.01.024.017.037.021a.09.09%200%2000.043%200%204.55%204.55%200%20013.046.275l.047.022.116.057a4.581%204.581%200%20012.188%202.399c.209.51.313%201.041.315%201.595a4.24%204.24%200%2001-.134%201.223.123.123%200%2000.03.115c.594.607.988%201.33%201.183%202.17.289%201.425-.007%202.71-.887%203.854l-.136.166a4.548%204.548%200%2001-2.201%201.388.123.123%200%2000-.081.076c-.191.551-.383%201.023-.74%201.494-.9%201.187-2.222%201.846-3.711%201.838-1.187-.006-2.239-.44-3.157-1.302a.107.107%200%2000-.105-.024c-.388.125-.78.143-1.204.138a4.441%204.441%200%2001-1.945-.466%204.544%204.544%200%2001-1.61-1.335c-.152-.202-.303-.392-.414-.617a5.81%205.81%200%2001-.37-.961%204.582%204.582%200%2001-.014-2.298.124.124%200%2000.006-.056.085.085%200%2000-.027-.048%204.467%204.467%200%2001-1.034-1.651%203.896%203.896%200%2001-.251-1.192%205.189%205.189%200%2001.141-1.6c.337-1.112.982-1.985%201.933-2.618.212-.141.413-.251.601-.33.215-.089.43-.164.646-.227a.098.098%200%2000.065-.066%204.51%204.51%200%2001.829-1.615%204.535%204.535%200%20011.837-1.388zm3.482%2010.565a.637.637%200%20000%201.272h3.636a.637.637%200%20100-1.272h-3.636zM8.462%209.23a.637.637%200%2000-1.106.631l1.272%202.224-1.266%202.136a.636.636%200%20101.095.649l1.454-2.455a.636.636%200%2000.005-.64L8.462%209.23z%27%20fill%3D%27url%28%23lobe-icons-codex-_R_0_%29%27%3E%3C/path%3E%3Cdefs%3E%3ClinearGradient%20gradientUnits%3D%27userSpaceOnUse%27%20id%3D%27lobe-icons-codex-_R_0_%27%20x1%3D%2712%27%20x2%3D%2712%27%20y1%3D%273%27%20y2%3D%2721%27%3E%3Cstop%20stop-color%3D%27%23B1A7FF%27%3E%3C/stop%3E%3Cstop%20offset%3D%27.5%27%20stop-color%3D%27%237A9DFF%27%3E%3C/stop%3E%3Cstop%20offset%3D%271%27%20stop-color%3D%27%233941FF%27%3E%3C/stop%3E%3C/linearGradient%3E%3C/defs%3E%3C/svg%3E",
  AGENT_CURSOR: "data:image/svg+xml,%3Csvg%20width%3D%271200%27%20height%3D%271200%27%20viewBox%3D%270%200%201200%201200%27%20fill%3D%27none%27%20xmlns%3D%27http%3A//www.w3.org/2000/svg%27%3E%3Crect%20width%3D%271200%27%20height%3D%271200%27%20rx%3D%27260%27%20fill%3D%27%239046FF%27/%3E%3Cmask%20id%3D%27mask0_1106_4856%27%20style%3D%27mask-type%3Aluminance%27%20maskUnits%3D%27userSpaceOnUse%27%20x%3D%27272%27%20y%3D%27202%27%20width%3D%27655%27%20height%3D%27796%27%3E%3Cpath%20d%3D%27M926.578%20202.793H272.637V997.857H926.578V202.793Z%27%20fill%3D%27white%27/%3E%3C/mask%3E%3Cg%20mask%3D%27url%28%23mask0_1106_4856%29%27%3E%3Cpath%20d%3D%27M398.554%20818.914C316.315%201001.03%20491.477%201046.74%20620.672%20940.156C658.687%201059.66%20801.052%20970.473%20852.234%20877.795C964.787%20673.567%20919.318%20465.357%20907.64%20422.374C827.637%20129.443%20427.623%20128.946%20358.8%20423.865C342.651%20475.544%20342.402%20534.18%20333.458%20595.051C328.986%20625.86%20325.507%20645.488%20313.83%20677.785C306.873%20696.424%20297.68%20712.819%20282.773%20740.645C259.915%20783.881%20269.604%20867.113%20387.87%20823.883L399.051%20818.914H398.554Z%27%20fill%3D%27white%27/%3E%3Cpath%20d%3D%27M636.123%20549.353C603.328%20549.353%20598.359%20510.097%20598.359%20486.742C598.359%20465.623%20602.086%20448.977%20609.293%20438.293C615.504%20428.852%20624.697%20424.131%20636.123%20424.131C647.555%20424.131%20657.492%20428.852%20664.447%20438.541C672.398%20449.474%20676.623%20466.12%20676.623%20486.742C676.623%20525.998%20661.471%20549.353%20636.375%20549.353H636.123Z%27%20fill%3D%27black%27/%3E%3Cpath%20d%3D%27M771.24%20549.353C738.445%20549.353%20733.477%20510.097%20733.477%20486.742C733.477%20465.623%20737.203%20448.977%20744.41%20438.293C750.621%20428.852%20759.814%20424.131%20771.24%20424.131C782.672%20424.131%20792.609%20428.852%20799.564%20438.541C807.516%20449.474%20811.74%20466.12%20811.74%20486.742C811.74%20525.998%20796.588%20549.353%20771.492%20549.353H771.24Z%27%20fill%3D%27black%27/%3E%3C/g%3E%3C/svg%3E",
  AGENT_KIRO: "data:image/svg+xml,%3Csvg%20viewBox%3D%270%200%20120%20120%27%20fill%3D%27none%27%20xmlns%3D%27http%3A//www.w3.org/2000/svg%27%3E%3Cdefs%3E%3ClinearGradient%20id%3D%27lobster-gradient%27%20x1%3D%270%25%27%20y1%3D%270%25%27%20x2%3D%27100%25%27%20y2%3D%27100%25%27%3E%3Cstop%20offset%3D%270%25%27%20stop-color%3D%27%23ff4d4d%27/%3E%3Cstop%20offset%3D%27100%25%27%20stop-color%3D%27%23991b1b%27/%3E%3C/linearGradient%3E%3C/defs%3E%3C%21--%20Body%20--%3E%3Cpath%20d%3D%27M60%2010%20C30%2010%2015%2035%2015%2055%20C15%2075%2030%2095%2045%20100%20L45%20110%20L55%20110%20L55%20100%20C55%20100%2060%20102%2065%20100%20L65%20110%20L75%20110%20L75%20100%20C90%2095%20105%2075%20105%2055%20C105%2035%2090%2010%2060%2010Z%27%20fill%3D%27url%28%23lobster-gradient%29%27/%3E%3C%21--%20Left%20Claw%20--%3E%3Cpath%20d%3D%27M20%2045%20C5%2040%200%2050%205%2060%20C10%2070%2020%2065%2025%2055%20C28%2048%2025%2045%2020%2045Z%27%20fill%3D%27url%28%23lobster-gradient%29%27/%3E%3C%21--%20Right%20Claw%20--%3E%3Cpath%20d%3D%27M100%2045%20C115%2040%20120%2050%20115%2060%20C110%2070%20100%2065%2095%2055%20C92%2048%2095%2045%20100%2045Z%27%20fill%3D%27url%28%23lobster-gradient%29%27/%3E%3C%21--%20Antenna%20--%3E%3Cpath%20d%3D%27M45%2015%20Q35%205%2030%208%27%20stroke%3D%27%23ff4d4d%27%20stroke-width%3D%273%27%20stroke-linecap%3D%27round%27/%3E%3Cpath%20d%3D%27M75%2015%20Q85%205%2090%208%27%20stroke%3D%27%23ff4d4d%27%20stroke-width%3D%273%27%20stroke-linecap%3D%27round%27/%3E%3C%21--%20Eyes%20--%3E%3Ccircle%20cx%3D%2745%27%20cy%3D%2735%27%20r%3D%276%27%20fill%3D%27%23050810%27/%3E%3Ccircle%20cx%3D%2775%27%20cy%3D%2735%27%20r%3D%276%27%20fill%3D%27%23050810%27/%3E%3Ccircle%20cx%3D%2746%27%20cy%3D%2734%27%20r%3D%272.5%27%20fill%3D%27%2300e5cc%27/%3E%3Ccircle%20cx%3D%2776%27%20cy%3D%2734%27%20r%3D%272.5%27%20fill%3D%27%2300e5cc%27/%3E%3C/svg%3E",
  AGENT_OPENCLAW: "data:image/svg+xml,%3Csvg%20xmlns%3D%27http%3A//www.w3.org/2000/svg%27%20version%3D%271.1%27%20xmlns%3Axlink%3D%27http%3A//www.w3.org/1999/xlink%27%20width%3D%27512%27%20height%3D%27512%27%3E%3Csvg%20width%3D%27512%27%20height%3D%27512%27%20viewBox%3D%270%200%20512%20512%27%20fill%3D%27none%27%20xmlns%3D%27http%3A//www.w3.org/2000/svg%27%3E%3Crect%20width%3D%27512%27%20height%3D%27512%27%20fill%3D%27%23131010%27%3E%3C/rect%3E%3Cpath%20d%3D%27M320%20224V352H192V224H320Z%27%20fill%3D%27%235A5858%27%3E%3C/path%3E%3Cpath%20fill-rule%3D%27evenodd%27%20clip-rule%3D%27evenodd%27%20d%3D%27M384%20416H128V96H384V416ZM320%20160H192V352H320V160Z%27%20fill%3D%27white%27%3E%3C/path%3E%3C/svg%3E%3Cstyle%3E%40media%20%28prefers-color-scheme%3A%20light%29%20%7B%20%3Aroot%20%7B%20filter%3A%20none%3B%20%7D%20%7D%20%40media%20%28prefers-color-scheme%3A%20dark%29%20%7B%20%3Aroot%20%7B%20filter%3A%20none%3B%20%7D%20%7D%20%3C/style%3E%3C/svg%3E",
  HERMES: "data:image/svg+xml,%3Csvg%20fill%3D%22%23ffffff%22%20fill-rule%3D%22evenodd%22%20height%3D%221em%22%20style%3D%22flex%3Anone%3Bline-height%3A1%22%20viewBox%3D%220%200%2024%2024%22%20width%3D%221em%22%20xmlns%3D%22http%3A//www.w3.org/2000/svg%22%3E%3Ctitle%3ENousResearch%3C/title%3E%3Cpath%20d%3D%22M5.938%2012.835c.127-.039.285.02.373.143.028.038.036.092.046.14.003.014-.02.033-.04.05-.124-.098-.24-.194-.354-.291-.011-.01-.016-.027-.025-.042zM8.396%209.412c.195-.032.39-.06.588-.05a.54.54%200%2001.148.026c.202.071.402.147.601.224.028.01.05.036.075.055l-.013.027a9.203%209.203%200%2001-.26-.089c-.115-.038-.213-.077-.315-.098-.25-.05-.25-.046-.292-.014l.574.144c.275.139.55.276.823.417.042.022.09.057.107.098.026.06.063.076.117.072.066-.006.132-.017.213-.027l-.04.086c.051.08.142.02.216.064-.074.13-.247.09-.334.199l.061.074-.12.087c0%20.106-.038.168-.306.243l.026.085-.196.042.07.124h-.25l-.007.137c-.081-.01-.161-.018-.244-.027l-.053.123c-.027-.008-.052-.011-.073-.023-.067-.038-.128-.056-.195.006-.019.017-.063.014-.093.008-.026-.006-.05-.029-.07-.042-.11.095-.11.095-.208.003-.057.046-.12.074-.186.011-.063.027-.123-.02-.178-.014-.07.007-.097-.035-.133-.07l-.13.033c-.013-.236-.194-.19-.34-.203.005-.072.05-.092.095-.094a.474.474%200%2001.159.022c.164.05.32.12.496.138.203.021.405.029.601-.015.265-.059.52-.149.707-.365.049-.056.083-.127.117-.195.019-.038.02-.084-.02-.116a1.397%201.397%200%2000-.382-.217c.024.12-.031.182-.115.221%200%20.014-.004.025%200%20.03.08.115.084.16-.007.267a1.39%201.39%200%2001-.218.211.477.477%200%2001-.641-.05%201.36%201.36%200%2001-.133-.152c-.078-.107-.076-.108-.033-.236-.165-.08-.128-.226-.104-.364.008-.05.028-.096.049-.163-.04.014-.067.017-.087.032a.897.897%200%2000-.316.357c-.007.016-.01.034-.02.047-.012.015-.034.038-.045.035-.02-.006-.037-.027-.05-.045-.008-.012-.007-.032-.012-.057h-.126l.053-.172a14.82%2014.82%200%2000-.039-.049l.11-.284c-.06.026-.091.044-.124.051-.03.007-.064%200-.095%200%200-.031-.01-.07.004-.092.149-.22.305-.428.593-.476z%22%3E%3C/path%3E%3Cpath%20d%3D%22M8.06%2010.788c-.003-.038-.004-.075.037-.062.016.006.034.048.028.067-.01.04-.038.032-.064-.005z%22%3E%3C/path%3E%3Cpath%20clip-rule%3D%22evenodd%22%20d%3D%22M11.981.009c.226-.012.453-.011.679%200%20.247.01.495.024.74.062.401.064.798.157%201.19.273.463.138.92.299%201.356.511a7.31%207.31%200%20012.948%202.642c.292.469.536.963.739%201.479.219.556.446%201.11.623%201.683.204.654.329%201.326.458%201.997.097.504.182%201.01.29%201.511.156.722.329%201.44.494%202.16.186.812.4%201.615.63%202.415.102.355.193.713.282%201.072.11.436.202.876.254%201.323.031.278.066.557.073.837a7.56%207.56%200%2001-.017.88c-.037.413-.1.818-.226%201.212a5.017%205.017%200%2001-.915%201.649l-.13.156.018.023c.043-.023.088-.041.127-.068.2-.138.373-.307.531-.49.4-.46.721-.973.975-1.529a3.59%203.59%200%2000.325-1.72c-.024-.424-.097-.834-.3-1.213-.013-.027-.015-.06-.03-.121.05.035.082.048.101.072.107.13.22.258.315.398.33.494.46%201.052.486%201.64a3.75%203.75%200%2001-.47%201.97c-.36.655-.887%201.14-1.526%201.506-.193.111-.394.21-.595.308-.157.078-.248.211-.318.365a.522.522%200%2000-.033.406.359.359%200%2001.013.139c-.005.077-.077.155-.14.162-.054.006-.125-.043-.15-.116a1.206%201.206%200%2001-.06-.233c-.04-.314-.155-.6-.308-.87a3.906%203.906%200%2000-.73-.91%202.129%202.129%200%2000-.897-.524%204.093%204.093%200%2000-.692-.131c-.075-.008-.15-.04-.22.01.18.06.363.11.538.18.434.173.82.43%201.18.728.308.255.58.543.794.884.098.155.186.315.227.496.027.123.042.25.067.375.013.062-.002.109-.053.144-.047.033-.122.034-.163-.01a.455.455%200%2001-.08-.14c-.03-.073-.038-.159-.078-.225a7.314%207.314%200%2000-1.423-1.664c-.16-.137-.329-.26-.537-.323-.376-.114-.753-.203-1.15-.154-.213.025-.427.032-.64.053a1.6%201.6%200%2000-.736.278%205.14%205.14%200%2000-.834.72c-.329.342-.642.699-.955%201.055-.136.155-.264.319-.314.531a5.227%205.227%200%2000-.012.051.096.096%200%2001-.09.076h-.31c-.046%200-.082-.048-.072-.094.023-.108.045-.216.07-.324.075-.325.19-.635.368-.917.024-.039.04-.088.104-.08l.01.049.027.077c.28-.435.571-.834.996-1.135.283-.204.584-.378.89-.55a.196.196%200%2000-.098-.002c-.162.043-.325.084-.485.134-.402.124-.764.33-1.11.566-.147.1-.298.193-.414.333a7.314%207.314%200%2000-1.07%201.767.845.845%200%2000-.04.12.075.075%200%2001-.072.056h-.494c-.04%200-.062-.051-.036-.082.123-.14.246-.282.377-.415.275-.281.58-.532.777-.884.027-.048.063-.09.095-.135.238-.333.54-.607.818-.902.082-.086.175-.16.26-.24.029-.027.053-.057.079-.085l-.018-.025-.135.041c-.034.017-.07.031-.102.05-.248.144-.494.292-.743.433-.408.23-.825.439-1.209.711-.281.2-.591.358-.889.533-.02.012-.044.015-.08.028-.015-.135.143-.201.108-.336-.033.014-.064.02-.085.038-.111.096-.227.19-.328.296-.148.157-.284.325-.425.488-.125.143-.25.286-.373.431A.153.153%200%20019.89%2024H8.762a.316.316%200%2000.016-.042c.028-.09.085-.172.083-.28-.091-.018-.162.001-.212.077a4.45%204.45%200%2000-.136.215c-.01.016-.024.03-.042.03h-.093c-.019%200-.029-.022-.017-.037.071-.088.14-.178.209-.268.001-.002-.006-.012-.012-.024-.014.004-.03.006-.045.013-.176.09-.352.181-.527.274a.363.363%200%2001-.168.042H5.202c-.026%200-.039-.036-.019-.053.21-.178.402-.374.558-.605.335-.496.538-1.047.667-1.629.004-.02-.003-.043-.006-.091-.037.048-.059.072-.076.1a1.943%201.943%200%2001-.334.415c-.28.258-.59.448-.983.464-.297.012-.588%200-.865-.127-.46-.21-.722-.57-.794-1.072-.025-.17-.017-.171-.182-.219A3.513%203.513%200%20011.97%2020.6a2.286%202.286%200%2001-.808-1.13%203.569%203.569%200%2001-.16-1.245c.002-.034.016-.067.024-.1.032.023.046.043.05.066.033.153.059.308.096.46.086.355.257.664.516.92.258.256.571.419.91.532.358.118.717.138%201.07-.016a1.89%201.89%200%2000.621-.452c.328-.348.533-.76.648-1.223.009-.034.005-.071.007-.11-.015.006-.026.006-.03.011-.031.05-.064.1-.093.152-.284.502-.679.887-1.196%201.135-.351.17-.718.255-1.11.159a1.607%201.607%200%2001-.971-.64%202.006%202.006%200%2001-.368-.924%202.903%202.903%200%2001.02-.886c.05-.439.466-1.17.742-1.271-.02.063-.035.112-.053.16-.043.116-.097.227-.13.345a1.901%201.901%200%2000-.05.82c.033.212.09.416.204.6.147.236.346.407.62.465.11.023.225.014.338.018a.576.576%200%2000.386-.131c.164-.128.282-.292.366-.481.168-.375.24-.777.309-1.179.05-.296.093-.594.133-.893.039-.281.071-.563.104-.845.026-.232.048-.464.074-.696.024-.228.052-.455.076-.683.024-.227.047-.455.069-.683.013-.14.022-.28.034-.42l.037-.417c.022-.25.041-.5.065-.748.008-.082-.02-.132-.09-.177a2.46%202.46%200%2001-.492-.418c-.1-.109-.188-.228-.282-.342-.035-.042-.056-.097-.116-.118a2.084%202.084%200%2000.275.597c.06.092.131.176.196.265.063.086.182.115.234.226-.028.003-.046.01-.06.006a4.74%204.74%200%2001-.22-.057%202.71%202.71%200%2001-1.287-.819c-.435-.487-.656-1.076-.71-1.723a5.206%205.206%200%2001.014-1.06c.072-.602.22-1.186.45-1.745.155-.376.338-.741.526-1.102.205-.393.466-.75.765-1.076.512-.559%201.104-1.024%201.726-1.448.717-.49%201.478-.898%202.277-1.233C8.244.828%208.767.632%209.31.494c.655-.166%201.31-.33%201.982-.415.229-.03.458-.058.688-.07zm-1.847%2022.82c-.07.06-.147.111-.207.18-.238.27-.464.549-.668.869l-.044.108a.177.177%200%2000.093-.057c.174-.19.351-.378.519-.574.104-.122.195-.255.288-.386.024-.034.03-.08.046-.12l-.027-.02zm1.65-3.695a5.51%205.51%200%2000-.653.593l-.37.386a.963.963%200%2001-.377.25%201.372%201.372%200%2001-.467.09c-.044%200-.087.006-.151.012.028.058.043.097.064.131.15.242.301.482.45.724.136.22.276.438.399.666.068.125.105.267.156.404.077.027.14-.018.202-.048.29-.135.579-.274.867-.412.213-.101.437-.186.636-.31.347-.215.68-.455%201.018-.685.015-.01.026-.028.042-.046-.023-.019-.038-.037-.056-.044-.287-.111-.527-.3-.77-.482a5.319%205.319%200%2001-.506-.42%201.757%201.757%200%2001-.41-.653c-.019-.049-.045-.095-.075-.156zm-5.847.264c-.06.096-.097.194-.132.293a3.38%203.38%200%2001-.555%201.01c-.2.25-.455.412-.762.493-.23.06-.464.076-.7.07-.048-.002-.097.002-.158.005.016.04.021.066.035.085.1.145.23.246.4.295.157.046.316.034.498.023.181-.037.343-.115.485-.234.238-.199.402-.454.536-.732.175-.363.264-.751.342-1.144.01-.053.008-.11.011-.164zm14.945-4.586c.008.029.016.057.027.107.024.155.051.31.072.464.03.219.067.437.078.657.017.344.027.689-.014%201.033-.037.315-.063.633-.116.946a6.153%206.153%200%2001-.46%201.518c-.008.018-.01.039-.02.082.047-.03.077-.042.098-.064.085-.083.17-.167.248-.255.271-.305.458-.66.596-1.043.18-.498.228-1.011.145-1.531-.103-.65-.33-1.263-.597-1.881a9.055%209.055%200%2000-.024-.055l-.033.022zM5.797%208.29a.26.26%200%2000.018.153c.124.251.25.501.379.75.025.049.066.09.03.163-.284.06-.578.119-.88.255.059.038.097.06.132.087.042.032.112.058.09.12-.01.033-.075.048-.117.072.017.01.043.021.067.036.166.102.33.207.447.368.138.192.229.404.188.644-.079.469-.306.85-.69%201.132-.054.04-.106.083-.161.122a.243.243%200%2000-.103.245.77.77%200%2000.055.195c.083.196.22.35.375.492.083.076.159.164.222.257a.37.37%200%2001.025.377c-.023.05-.05.099-.076.148-.03.06-.028.111.022.162.041.042.08.089.112.138.038.058.078.079.147.05a.486.486%200%2001.333-.006c.16.046.302.126.444.21.13.077.264.149.4.219.067.035.14.05.219.026.071-.022.124.01.145.076.02.064-.003.108-.074.139-.07.03-.137.063-.209.088-.1.035-.201.073-.314.077-.013-.107.11-.088.127-.159-.206-.126-.643-.145-.801-.034.063.112.035.21-.096.313-.13-.1-.025-.202.002-.3a.209.209%200%2000-.249.17c-.015.101.067.216.178.224.108.007.218-.005.326-.012.06-.005.12-.027.199%200-.103.123-.248.127-.357.19.002.05.07.086.019.131-.053.048-.095-.001-.132-.03-.08-.063-.16-.126-.231-.197a.474.474%200%2001-.157-.311.52.52%200%2000-.043-.172c-.032-.074-.032-.137.033-.19-.018-.03-.028-.053-.045-.072a1.222%201.222%200%2001-.196-.369c-.053-.137-.046-.264.048-.381.024-.03.05-.06.064-.095a.664.664%200%2000.047-.168c.017-.165-.064-.287-.182-.387-.186-.156-.36-.322-.46-.551-.005-.011-.024-.017-.037-.026-.011.017-.024.027-.025.038-.019.185-.045.37-.052.557-.014.377.058.743.162%201.104.118.41.289.798.488%201.173.267.502.537%201.002.812%201.5.055.098.13.189.208.27.198.202.452.272.724.273.202%200%20.404-.006.605-.026.295-.03.59-.073.884-.113.183-.025.365-.057.548-.08.21-.026.38.073.522.21.16.156.305.327.447.5.22.265.397.56.554.867.05.098.07.1.147.03.13-.121.26-.242.394-.36.067-.059.088-.12.067-.213a3.535%203.535%200%2001-.085-.796c.002-.157.006-.314.018-.471.015-.224.03-.45.06-.672a59.114%2059.114%200%2001.362-2.298c.087-.493.182-.984.268-1.477.06-.347.118-.694.162-1.043.034-.273.055-.55.063-.825.011-.332.003-.665.002-.998%200-.077.004-.155-.01-.23-.028-.142-.01-.155-.162-.19a5.826%205.826%200%2000-.607-.107c-.146-.018-.207-.053-.221-.19-.006-.049-.025-.098-.041-.146-.009-.025-.024-.048-.046-.09l-.025.264c-.009.096-.029.116-.127.115-.055%200-.11-.008-.164-.008-.476%200-.952-.008-1.426.032-.095.008-.173-.015-.226-.103-.04-.066-.088-.126-.134-.186-.063-.084-.086-.093-.182-.06-.195.068-.388.138-.582.21a2.71%202.71%200%2000-.675.394.986.986%200%2001-.323.168c-.033.01-.07.008-.127.013.02-.066.024-.114.047-.15.064-.105.135-.205.205-.306.023-.033.049-.063.073-.095l-.015-.023-.201.037c-.146.04-.296.07-.437.122-.148.053-.266.023-.386-.072a3.623%203.623%200%2001-.733-.786l-.093-.132zm8.592%208.963l-.147.09c-.22.134-.44.266-.659.402-.093.058-.184.12-.27.188-.085.07-.124.161-.072.272.047.1.093.2.147.294.047.08.124.138.213.147.11.01.228.012.336-.012.217-.05.372-.205.528-.357a.291.291%200%2000.087-.308c-.046-.18-.079-.365-.118-.547-.011-.052-.027-.103-.045-.169zm-.257-2.409c-.12.291-.205.597-.325.91-.151.433-.294.87-.435%201.323.036-.01.054-.01.067-.018.261-.16.522-.324.785-.484.054-.033.071-.078.065-.138-.012-.13-.024-.262-.034-.393l-.068-.886c-.008-.103-.02-.206-.029-.31-.009%200-.017-.002-.026-.004zm3.081-8.13l.099.285c.08.231.159.463.24.714l.58%201.952c.187.63.372%201.262.558%201.893.114.382.235.762.343%201.146.072.257.126.519.186.799.044.206.087.413.127.64.034.106.023.226.077.325l.025-.006-.068-.362c-.038-.206-.077-.412-.113-.638-.015-.07-.029-.141-.046-.211-.095-.396-.177-.796-.29-1.187-.196-.685-.413-1.364-.618-2.046-.165-.549-.322-1.1-.488-1.648-.069-.227-.15-.45-.226-.695l-.117-.336c-.037-.107-.075-.216-.115-.322-.04-.106-.084-.21-.127-.314a7.558%207.558%200%2001-.027.01zM6.225%2014.304c-.063-.001-.115.014-.134.083a.35.35%200%2000.41.012%204.533%204.533%200%2000-.276-.095zM5.23%2011.98c-.026-.027-.057-.048-.075.002-.012.032-.007.07-.01.113.082-.037.082-.037.085-.115zm.062-1.189a.135.135%200%2000-.088.056.197.197%200%2000-.025.11c.005.152.01.306.026.457a.751.751%200%2000.066.218c.061.136.157.167.288.101.055-.027.06-.054.025-.11a4.52%204.52%200%2001-.129-.211c-.015-.068-.066-.131-.033-.207.04-.09-.076-.116-.074-.19V10.874c-.003-.038-.006-.087-.056-.083zm-.017-.968a.867.867%200%2000-.467.127c-.076.045-.084.07-.05.158.034.087.07.173.115.254.064.117.09.125.21.077a.657.657%200%2001.336-.053c.202.022.357.136.504.264l.092.077c.007-.006.014-.013.022-.018-.019-.105-.035-.226-.149-.264-.157-.053-.324-.075-.508-.117l-.24-.005c.24-.169.452-.044.687.009-.063-.115-.153-.147-.23-.193-.082-.05-.17-.092-.25-.144-.06-.037-.12-.08-.072-.172zm10.233.325c-.23-.01-.427.08-.608.211-.034.026-.06.065-.105.117.087.026.15.046.232.065.044-.015.088-.03.13-.046.306-.114.61-.115.904.031.126.063.237.04.366-.005-.02-.031-.03-.054-.045-.071a.986.986%200%2000-.448-.273c-.14-.044-.284-.024-.426-.03zM7.99%206.483a.308.308%200%2000.002.133c.08.321.156.643.242.962.104.387.27.75.456%201.103.02.037.061.08.098.087a.404.404%200%2000.253-.051l-.472-.84c-.23-.448-.405-.92-.579-1.394zM10.397.497c-.2-.008-.405.004-.603.034-.236.035-.47.087-.7.152-.287.08-.569.18-.852.273-.04.013-.074.038-.11.058.028.014.05.018.07.014.287-.068.58-.085.873-.09.134-.002.269.009.402.025.19.024.382.048.57.09.456.104.874.3%201.265.556.464.306.888.66%201.257%201.078.205.232.395.475.56.739.17.274.315.561.449.856.273.601.456%201.232.6%201.876.04.173.07.348.1.524.017.104.065.167.17.19.122.028.2.105.22.251-.003.102-.06.174-.129.24a1.065%201.065%200%2000-.268.358.164.164%200%2000.083-.039c.08-.086.162-.172.235-.265a.56.56%200%2000.13-.333c.009-.05.022-.1.024-.15.007-.124-.017-.15-.143-.168-.025-.004-.049-.014-.073-.015-.082-.007-.125-.063-.137-.131-.033-.198-.004-.355.247-.408.086-.018.174-.03.26-.042.158-.023.315-.053.473-.067.14-.012.19.033.226.167.008.029.018.057.021.087.019.179-.008.225-.141.288-.027.013-.055.024-.078.042a.148.148%200%2000-.051.067c-.039.144.073.382.206.445l.673.32c.023.011.05.015.075.023l.018-.026c-.015-.008-.032-.013-.044-.024a2.27%202.27%200%2000-.544-.32%204.898%204.898%200%2000-.173-.075.203.203%200%2001-.126-.191c-.003-.085.045-.154.128-.187l.059-.025c.099-.044.118-.076.112-.187a.384.384%200%2000-.008-.063c-.067-.294-.123-.59-.205-.88a9.478%209.478%200%2000-.826-2.036%207.465%207.465%200%2000-1.39-1.805%204.536%204.536%200%2000-1.177-.824%203.656%203.656%200%2000-1.016-.328%206.155%206.155%200%2000-.712-.074zm6.719%205.955c.01.014.018.028.038.034l-.022-.044-.016.01zM4.103%203.917a.062.062%200%2001-.03.012.455.455%200%2001-.04.039c-.01.01-.02.02-.045.04l-.363.354c-.088.085-.17.178-.266.253-.284.22-.425.53-.544.855a.132.132%200%2000-.007.071c.013.055.033.108.052.168l.074.026c-.017.056-.03.105-.047.152-.058.164-.118.327-.175.491-.005.015.008.036.019.077.08-.175.158-.33.225-.489.228-.544.484-1.074.819-1.561.09-.133.182-.266.283-.401.004-.006.007-.013.022-.03.001-.016.003-.032.015-.04l.008-.017zm12.976%202.408a.023.023%200%2001.009.019.073.073%200%2000-.006.01.188.188%200%2000.007.02l.018.022c.002-.007.007-.016.005-.021-.003-.01-.012-.018-.02-.038a1.331%201.331%200%2001-.013-.012zM4.199%204.48c-.003.004-.008.008-.027.014-.005.013-.011.025-.031.047a2.085%202.085%200%2001-.124.167c-.048.07-.116.055-.181.041-.134-.028-.228.016-.287.143-.089.187-.187.37-.273.56-.049.108-.11.216-.118.36.081.003.154.007.228.008h.228a2.563%202.563%200%2001-.079.264c-.01.052-.022.103-.033.155l.02.004c.018-.046.037-.092.067-.153.066-.142.13-.285.2-.426.02-.04.034-.1.116-.092%200%20.043.004.084%200%20.124-.005.045-.017.09-.028.143.141.043.086.174.115.269.102-.022.104-.195.248-.144v.205l.017.002.439-1.059c-.13%200-.246-.02-.358.033-.024.011-.058-.001-.108-.004.075-.15.139-.278.211-.417a.128.128%200%2001.025-.036c0-.015-.001-.03.008-.038l.006-.02c-.005.006-.01.011-.028.017-.004.012-.009.024-.026.045a.085.085%200%2001-.032.033c-.123.157-.09.164-.258.106-.079-.027-.078-.028-.047-.144.028-.046.056-.093.098-.15%200-.016-.001-.032.007-.042L4.2%204.48zm2.073-.67c-.003.006-.007.011-.027.016-.094.125-.194.246-.28.377-.155.238-.301.481-.451.723-.14.224-.345.368-.575.481-.017.008-.04.006-.079.011.012-.059.016-.109.033-.153a6.076%206.076%200%2001.229-.518l-.007-.02a.138.138%200%2001-.035.025c-.028.05-.055.1-.093.164-.26.424-.443.817-.442.95.024.004.048.011.073.013.177.013.188.007.26-.165.03-.07.077-.12.147-.15l.175-.07c.044-.018.085-.057.146-.032.003.05-.01.11.014.145.042.062.044.125.047.193.002.049.017.098.026.147.029-.034.039-.065.05-.097.142-.39.277-.782.428-1.17.1-.256.22-.504.33-.756.013-.03.013-.067.03-.092V3.81zm3.987-.34c0%20.045.01.084.021.123.042.16.094.318.124.48.024.133.023.27.028.406%200%20.033-.019.067-.032.11-.094-.058-.047-.158-.106-.215h-.125c-.015.072-.01.152-.046.2-.066.085-.155.154-.236.227-.043.038-.078.018-.103-.025l-.046-.087c-.065.035-.117.069-.172.093-.116.051-.235.095-.35.147-.085.038-.09.053-.07.147.014.075.034.148.047.223.013.072.05.109.123.124.233.05.462.115.657.265.058-.102.058-.102.168-.151.03-.014.06-.03.092-.042.08-.03.115-.017.15.06.023.048.041.098.066.158.06-.14-.042-.267.017-.416.157.18.24.39.375.567a.235.235%200%2000.022-.098c.002-.124%200-.247.002-.371%200-.034.013-.067.02-.1l.032-.003c.11.155.13.354.226.52a3.036%203.036%200%2000-.01-.392c-.004-.045%200-.074.05-.088.08.036.116.14.215.158-.03-.275-.423-1.137-.798-1.635-.114-.127-.2-.28-.34-.386zm-2.667.696c-.019.034-.03.05-.037.067-.061.185-.125.37-.18.556-.031.105-.087.169-.195.19-.09.019-.178.052-.268.073-.038.009-.089.015-.118-.003-.024-.016-.025-.069-.036-.106-.064.076-.082.087-.17.047-.133-.062-.262-.135-.393-.201-.048-.025-.093-.063-.17-.03-.043.12-.091.25-.137.382-.099.28-.087.242.095.453.046.048.102.03.154.023.054-.009.106-.03.16-.036.13-.013.26-.08.367-.015.204-.064.387-.122.571-.178.05-.015.089.005.114.054.022.042.034.093.082.121.038-.056-.013-.128.063-.178l.14.241-.042-1.46zm.278.358c-.096-.01-.107.01-.11.108-.002.038-.003.078.002.115.03.2.099.386.174.57.002.006.012.01.022.015l.078-.05c.052.036.081.088.153.088.205-.002.41.014.616.012.099-.001.158.042.205.12.018.03.024.077.088.066l-.08-.394c-.05-.195-.085-.395-.172-.589-.057.057-.114.068-.18.046a.72.72%200%2000-.135-.028c-.22-.028-.44-.059-.66-.08zm10.254-1.727c.089.163.155.316.139.491-.016.168.026.342-.044.516-.047-.033-.088-.082-.112-.075-.117.035-.164-.057-.227-.115a4.772%204.772%200%2001-.286-.29l-.104-.113a4.856%204.856%200%2001-.023.019c.035.046.07.093.11.156.04.064.084.127.122.193.034.058.065.118.031.205-.082-.01-.164-.019-.246-.032-.06-.01-.101%200-.124.07-.031.098-.037.096-.15.09.02.042.036.08.057.116.041.074.03.138-.03.196-.06.06-.118.122-.178.181a.175.175%200%2001-.185.046c-.222-.061-.447-.113-.67-.174-.032-.009-.063-.04-.086-.068-.03-.04-.052-.087-.08-.13-.044-.07-.09-.138-.136-.207a.18.18%200%2000-.014.105c.012.127.03.253.035.38.005.1-.024.12-.121.104-.104-.017-.206-.04-.31-.058-.064-.012-.131-.028-.202.03l.081.208c.09%200%20.166-.01.237.002a.819.819%200%2001.458.251c.078.083.154.168.241.26l.018-.005c-.004-.006-.008-.013-.01-.04.014-.056-.062-.118.018-.178.031.03.064.057.088.09.058.078.111.159.169.257l.089.141.024-.013a2093.819%202093.819%200%2001-.427-.934c.055.007.083.007.108.016.193.07.385.142.577.216.074.028.147.06.219.094.062.028.112.018.157-.033.05-.056.102-.112.154-.167.05-.051.095-.046.132.014.016.025.026.053.04.08.071.138.143.277.217.433l.159.308.025-.011c-.044-.106-.07-.218-.138-.334-.057-.182-.168-.346-.206-.545.136.034.362.326.567.732l.057.074.018-.011a1.563%201.563%200%2001-.052-.127c-.046-.145-.097-.29-.136-.436-.022-.083-.036-.173.022-.26l.109.058-.026-.207.027-.016c.022.02.05.036.065.06.073.108.143.22.215.33.01.016.029.029.043.043-.036-.217-.2-.38-.229-.626l.155.112c.014-.166.012-.319.042-.465.032-.158-.023-.297-.063-.445.024.004.036.006.055.025.092.124.183.249.277.371.02.027.05.047.069.087l.04.063.019-.015a.293.293%200%2001-.053-.082%2027.922%2027.922%200%2001-.332-.49c-.221-.311-.363-.467-.485-.521zm-6.57.327c-.003.161.092.275.069.415l-.368.087c.09.139.032.237-.052.331-.05.057-.092.122-.143.178-.037.04-.046.078-.018.126l.16.275c.029.048.072.066.128.064.076-.003.152%200%20.228-.001.116-.003.216.022.275.137.006.014.02.024.044.052.004-.059-.003-.098.01-.13.016-.04.04-.099.072-.108.084-.023.173-.024.26-.03.013-.001.027.018.04.029l.071.065c.019-.11-.082-.198-.024-.31l.126.04c-.026-.123-.07-.245-.071-.366%200-.123.051-.243.115-.36.107.062.16.156.234.253.183.265.36.533.494.834.165-.078.27.068.407.088-.003-.106-.133-.441-.197-.492a.142.142%200%2000-.102-.028c-.06.011-.119.039-.191.063-.025-.039-.056-.078-.077-.122a3.936%203.936%200%2000-.473-.783c-.076-.094-.16-.182-.228-.26l-.391.285c-.049.035-.094.03-.132-.017l-.169-.207c-.025-.03-.053-.059-.097-.108z%22%3E%3C/path%3E%3C/svg%3E",
};

// ---- import-flow shim: the first-run import UI lives in the onboarding flow,
// but the chrome.send handlers stay registered here so the fixture test keeps
// driving this page directly.
window.prismWelcome = {
  onStatus(text) { /* no visible status area on this page */ },
  onImportReport(reportJson) { window.__lastReport = JSON.parse(reportJson); },
};

const AGENTS = [
  { name: "Codex",       icon: ASSETS.AGENT_CODEX,        promptUrlPrefix: "codex://new?prompt=" },
  { name: "Claude Code", icon: ASSETS.AGENT_CLAUDE_CODE,  promptUrlPrefix: "claude://code/new?q=" },
  { name: "OpenCode",    icon: "assets/agent-opencode.png", appUrl: "opencode://open" },
  { name: "Cursor",      icon: ASSETS.AGENT_CURSOR,       promptUrlPrefix: "cursor://anysphere.cursor-deeplink/prompt?text=" },
  { name: "Hermes",      icon: ASSETS.HERMES,             appUrl: "hermes://open" },
];
const SPIN_ICONS = [
  ASSETS.AGENT_CODEX, ASSETS.AGENT_CLAUDE_CODE, ASSETS.AGENT_CURSOR,
  ASSETS.HERMES, ASSETS.AGENT_KIRO, ASSETS.AGENT_OPENCLAW,
  "assets/agent-opencode.png",
];

// SVGs that appear as static markup imgs get their data URIs assigned here.
document.getElementById("arcsImg").src = ASSETS.ARCS;
document.getElementById("agentIcon").src = ASSETS.AGENT_CODEX;
const PROMPT = "/prism-browser OpenAI & Anthropic blogs, summarize latest noteworthy updates";

// ---- header icon rotator (1.5s, like the original) ----
const spin = document.getElementById("agentSpin");
for (const src of SPIN_ICONS) {
  const img = document.createElement("img");
  img.src = src;
  img.alt = "";
  spin.appendChild(img);
}
let spinIndex = 0;
function tickSpin() {
  const imgs = spin.querySelectorAll("img");
  imgs.forEach((img, i) => img.classList.toggle("on", i === spinIndex));
  spinIndex = (spinIndex + 1) % SPIN_ICONS.length;
}
tickSpin();
setInterval(tickSpin, 1500);

// ---- agent dropdown + open-in ----
let current = AGENTS[0];
const menuBtn = document.getElementById("agentMenuBtn");
const menu = document.getElementById("agentMenu");
const pasteHint = document.getElementById("pasteHint");
for (const agent of AGENTS) {
  const btn = document.createElement("button");
  btn.type = "button";
  btn.setAttribute("role", "option");
  const img = document.createElement("img");
  img.src = agent.icon;
  img.alt = "";
  const name = document.createElement("span");
  name.textContent = agent.name;
  btn.appendChild(img);
  btn.appendChild(name);
  btn.addEventListener("click", (e) => {
    e.stopPropagation();
    current = agent;
    document.getElementById("agentIcon").src = agent.icon;
    document.getElementById("agentName").textContent = agent.name;
    pasteHint.hidden = agent.promptUrlPrefix != null;
    menu.classList.remove("open");
    menuBtn.setAttribute("aria-expanded", "false");
  });
  menu.appendChild(btn);
}
menuBtn.addEventListener("click", (e) => {
  e.stopPropagation();
  const open = menu.classList.toggle("open");
  menuBtn.setAttribute("aria-expanded", String(open));
});
document.addEventListener("click", () => menu.classList.remove("open"));
document.getElementById("openInOverlay").addEventListener("click", () => {
  const url = current.promptUrlPrefix
    ? current.promptUrlPrefix + encodeURIComponent(PROMPT)
    : current.appUrl;
  if (url) window.location.href = url;
});

// ---- copy prompt with Copied state (1.5s) ----
const copyBtn = document.getElementById("copyBtn");
let copyTimer = null;
copyBtn.addEventListener("click", async () => {
  try {
    await navigator.clipboard.writeText(PROMPT);
  } catch {
    const ta = document.createElement("textarea");
    ta.textContent = PROMPT;
    document.body.appendChild(ta);
    ta.select();
    document.execCommand("copy");
    ta.remove();
  }
  copyBtn.classList.add("copied");
  document.getElementById("copyIcon").hidden = true;
  document.getElementById("copiedIcon").hidden = false;
  copyBtn.setAttribute("aria-label", "Copied");
  if (copyTimer) clearTimeout(copyTimer);
  copyTimer = setTimeout(() => {
    copyBtn.classList.remove("copied");
    document.getElementById("copyIcon").hidden = false;
    document.getElementById("copiedIcon").hidden = true;
    copyBtn.setAttribute("aria-label", "Copy prompt");
  }, 1500);
});

// ---- dynamic spacers (port of the original ResizeObserver logic): keep the
// header-to-cards gap within [30, 100] and the bottom spacer within
// [0, 300] so the column breathes like the original page.
const mainEl = document.getElementById("main");
const brandEl = document.querySelector("header.brand");
const viewportEl = document.getElementById("cardsViewport");
const gapEl = document.getElementById("gapSpacer");
const bottomEl = document.getElementById("bottomSpacer");
const clamp = (v, lo, hi) => Math.min(Math.max(v, lo), hi);
function layout() {
  if (!mainEl || !brandEl || !viewportEl) return;
  const used = brandEl.getBoundingClientRect().height +
               viewportEl.getBoundingClientRect().height;
  const remaining = mainEl.clientHeight - used;
  gapEl.style.height = clamp(remaining, 30, 100) + "px";
  bottomEl.style.height = clamp(remaining - 100, 0, 300) + "px";
}
layout();
new ResizeObserver(layout).observe(mainEl);
)JS";

// --------------------------- Chrome profile import --------------------------
//
// The first-run import UI lives in the onboarding flow; the chrome.send
// handlers stay registered here so the fixture test can keep driving this
// page directly (it sends importFromChrome and reads window.__lastReport).
// The machinery is shared with chrome://prism-onboarding via
// chrome/browser/prism/prism_chrome_live_import.{h,cc}: staged full import
// (cookies/passwords/preferences/extensions — applied on next startup) plus
// a live bookmarks/history merge via ProfileWriter (no restart needed).

}  // namespace

PrismWelcomeUIConfig::PrismWelcomeUIConfig()
    : content::DefaultWebUIConfig<PrismWelcomeUI>(content::kChromeUIScheme,
                                                  "prism-welcome") {}
PrismWelcomeUIConfig::~PrismWelcomeUIConfig() = default;

PrismWelcomeUI::PrismWelcomeUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  importer_list_ = std::make_unique<ImporterList>();
  auto* browser_context = web_ui->GetWebContents()->GetBrowserContext();
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      browser_context, "prism-welcome");
  // The page inlines its SVG art as data: URIs (the request filter serves
  // bytes with a generic MIME type and <img> does not sniff SVG).
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ImgSrc, "img-src 'self' data:;");
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
            if (path.starts_with("assets/")) {
              if (const auto* asset =
                      ::PrismWelcome::FindPrismWelcomeAsset(path.substr(7))) {
                std::move(callback).Run(
                    base::MakeRefCounted<base::RefCountedStaticMemory>(
                        UNSAFE_BUFFERS(base::span<const uint8_t>(
                            asset->data, asset->size))));
                return;
              }
            }
            std::move(callback).Run(
                base::MakeRefCounted<base::RefCountedString>(kPageHtml));
          }));
  web_ui->RegisterMessageCallback(
      "importFromChrome",
      base::BindRepeating(&PrismWelcomeUI::OnImportFromChrome,
                          weak_factory_.GetWeakPtr()));
  web_ui->RegisterMessageCallback(
      "importFromOther",
      base::BindRepeating(
          [](base::WeakPtr<PrismWelcomeUI> self, const base::ListValue&) {
            if (!self) {
              return;
            }
            self->ReportStatus("Looking for Safari / Firefox profiles…");
            self->importer_list_->DetectSourceProfiles(
                "en-US", /*include_interactive_profiles=*/true,
                base::BindOnce(&PrismWelcomeUI::OnSourceProfilesDetected,
                               self));
          },
          weak_factory_.GetWeakPtr()));
  web_ui->RegisterMessageCallback(
      "restartNow",
      base::BindRepeating(
          [](const base::ListValue&) {
            // The staged import lands during the next startup, before the
            // profile opens those files; OSCrypt then derives its key from
            // the migrated seed.
            chrome::AttemptRestart();
          }));
}

PrismWelcomeUI::~PrismWelcomeUI() = default;

void PrismWelcomeUI::ReportStatus(const std::string& status) {
  web_ui()->CallJavascriptFunctionUnsafe("prismWelcome.onStatus",
                                         base::ValueView(status));
}

void PrismWelcomeUI::OnSourceProfilesDetected() {
  if (importer_list_->count() == 0) {
    ReportStatus("No Safari or Firefox profile found on this machine.");
    return;
  }
  const auto& source = importer_list_->GetSourceProfileAt(0);
  Profile* profile =
      Profile::FromBrowserContext(web_ui()->GetWebContents()->GetBrowserContext());
  auto* host = new ExternalProcessImporterHost();
  ReportStatus(std::string("Importing from ") +
               base::UTF16ToUTF8(source.importer_name) + "…");
  // Matches the settings-page call site's ownership idiom.
  host->StartImportSettings(source, profile,
                            user_data_importer::ALL,
                            new ProfileWriter(profile));
  ReportStatus("Import from " + base::UTF16ToUTF8(source.importer_name) +
               " started — a progress notification will follow.");
}

void PrismWelcomeUI::OnImportFromChrome(const base::ListValue& args) {
  Profile* profile =
      Profile::FromBrowserContext(web_ui()->GetWebContents()->GetBrowserContext());
  ReportStatus("Importing from Chrome… (macOS may ask once for keychain access)");
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&RunFullChromeImport, profile->GetPath()),
      base::BindOnce(
          [](base::WeakPtr<PrismWelcomeUI> self, Profile* profile,
             ChromeImportResult result) {
            if (!self) {
              return;
            }
            if (!result.live.chrome_found) {
              std::string json;
              base::JSONWriter::Write(base::Value(std::move(result.report)),
                                      &json);
              self->web_ui()->CallJavascriptFunctionUnsafe(
                  "prismWelcome.onImportReport", base::ValueView(std::move(json)));
              return;
            }
            base::DictValue report = MergeChromeImportLiveData(profile, result);
            std::string json;
            base::JSONWriter::Write(base::Value(std::move(report)), &json);
            self->web_ui()->CallJavascriptFunctionUnsafe(
                "prismWelcome.onImportReport", base::ValueView(std::move(json)));
          },
          weak_factory_.GetWeakPtr(), profile));
}

}  // namespace prism
