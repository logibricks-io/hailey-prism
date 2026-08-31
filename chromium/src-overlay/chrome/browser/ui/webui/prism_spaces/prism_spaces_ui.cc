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
#include "base/strings/utf_string_conversions.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/prism/prism_space_window_delegate.h"
#include "chrome/browser/prism/prism_spaces_ui_constants.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/devtools_agent_host_client.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "prism/browser/spaces/space_manager.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkImage.h"
#include "ui/gfx/codec/png_codec.h"

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
         margin: 0; padding: 24px 60px 88px; }
  /* recon §7/§8 enter-exit motion (~0.5-0.7s ease-out/spring) */
  /* recon §8: non-current cards are present almost immediately — subtle
     rise/fade, ~50ms stagger, no long right travel */
  @keyframes card-in-calm { from { opacity: 0; transform: translateY(10px); } }
  @keyframes fade-in { from { opacity: 0; } }
  @keyframes thumb-pop { from { opacity: 0; transform: scale(.92); } }
  .item.enter { animation: card-in-calm .45s cubic-bezier(.2,.8,.25,1)
                backwards; animation-delay: var(--d, 0ms); }
  #fxBackdrop { position: fixed; inset: 0; z-index: 99; background: #171717;
                opacity: 0; pointer-events: none;
                transition: opacity .25s ease; }
  header.enter { animation: fade-in .4s ease .25s backwards; }
  #hintBar.enter { animation: fade-in .4s ease .35s backwards; }
  .thumb img.pop { animation: thumb-pop .3s cubic-bezier(.2,.9,.3,1.15)
                   backwards; animation-delay: var(--d, 0ms); }
  #fxOverlay { position: fixed; z-index: 100; pointer-events: none;
               object-fit: cover; object-position: top; background: #101010; }
  .card.fx-self-hidden { opacity: 0; }

  /* recon §8: hosted window-wide (?window=1) the page has no header — the
     native top row (caption + corner trigger) replaces it. */
  body.window-mode header { display: none; }

  header { display: flex; align-items: center; height: 40px; }
  header .brand { display: inline-flex; align-items: center; gap: 8px;
                  color: var(--text-2); font-weight: 600; font-size: 14px; }
  header .side { flex: 1; }
  /* recon §4: "N Space(s) ⌄" sits top center (tab-hosted page only) */
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

  /* recon §7/§8: card row starts ≈8% from the top; uniform cards, 40px gaps */
  #wall { display: grid; grid-template-columns: repeat(auto-fill,
          minmax(340px, 1fr)); gap: 40px 40px;
          margin-top: max(12px, calc(8vh - 64px)); }
  body.window-mode #wall { margin-top: max(12px, calc(8vh - 24px)); }
  .item { min-width: 0; }
  /* The card is the thumbnail, edge to edge (recon §8): rounded, thin
     border, blue when focused. */
  .card { position: relative; border-radius: 14px; overflow: hidden;
          cursor: pointer; background: #101010; aspect-ratio: 16 / 10;
          border: 1px solid #ffffff0d;
          transition: border-color .15s, box-shadow .15s, transform .15s,
                      opacity .3s; }
  .card:hover { border-color: #ffffff26; transform: translateY(-2px); }
  .card.focused { border-color: var(--accent);
                  box-shadow: 0 0 0 1.5px var(--accent); }
  .thumb { position: absolute; inset: 0; }
  .thumb img { position: absolute; inset: 0; width: 100%; height: 100%;
               object-fit: cover; object-position: top; }
  /* recon §7/§8: BELOW the card (outside) — left "Space" label (agent
     cards: blue "Running" chip + task name), right the dynamic watermark */
  .cardfoot { display: flex; align-items: center; justify-content: space-between;
              gap: 10px; padding: 9px 4px 0; }
  .foot-left { display: inline-flex; align-items: center; gap: 8px;
               min-width: 0; overflow: hidden; }
  .space-label { font-size: 11.5px; font-weight: 600; color: var(--text-2);
                 white-space: nowrap; }
  .foot-task { font-size: 11.5px; color: var(--text-2); white-space: nowrap;
               overflow: hidden; text-overflow: ellipsis; }
  .chip { font-size: 11px; padding: 2px 8px; border-radius: 999px;
          border: 1px solid #ffffff1f; color: var(--text-3);
          white-space: nowrap; }
  .chip.running { color: var(--accent); border-color: var(--accent); }
  .watermark { color: var(--text-3); font-size: 11px; white-space: nowrap; }
  /* recon §8: the "+" card is plain dark with a single large plus */
  .newspace { display: flex; align-items: center; justify-content: center;
              color: var(--text-3); border: 1.5px dashed #ffffff21;
              background: transparent; }
  .newspace:hover { border-color: var(--accent); }
  .newspace .plus { font-size: 42px; font-weight: 300; color: var(--accent); }
  .empty { color: var(--text-3); }

  /* recon §4: first-run ⌥S hint bar (keyboard icon per recon §7) */
  #hintBar { position: fixed; left: 50%; bottom: 22px;
             transform: translateX(-50%); display: flex; align-items: center;
             gap: 12px; background: var(--fg-1); border: 1px solid #ffffff1a;
             border-radius: 999px; padding: 10px 12px 10px 18px;
             color: var(--text-2); font-size: 13px; z-index: 40;
             box-shadow: 0 10px 34px #00000066; }
  #hintBar svg { flex: none; opacity: .8; }
  #hintBar kbd { color: var(--text-1); font-family: inherit; font-weight: 600; }
  #hintBar button { border-radius: 50%; padding: 3px 8px;
                    color: var(--text-3); }
  button { background: #ffffff0f; color: var(--text-1); border: 0;
           border-radius: 8px; padding: 5px 12px; font-size: 12.5px;
           cursor: pointer; font-family: inherit; }
  button:hover { background: #ffffff1a; }
  button.danger:hover { background: #5d2f3d; }
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
  <svg width="20" height="20" viewBox="0 0 24 24" fill="none" aria-hidden="true"><rect x="2" y="5" width="20" height="14" rx="3" stroke="#ffffffb3" stroke-width="1.6"/><rect x="5" y="8.5" width="2.4" height="2.4" rx=".6" fill="#ffffffb3"/><rect x="9" y="8.5" width="2.4" height="2.4" rx=".6" fill="#ffffffb3"/><rect x="13" y="8.5" width="2.4" height="2.4" rx=".6" fill="#ffffffb3"/><rect x="17" y="8.5" width="2.4" height="2.4" rx=".6" fill="#ffffffb3"/><rect x="5" y="12.5" width="2.4" height="2.4" rx=".6" fill="#ffffffb3"/><rect x="9" y="12.5" width="8.4" height="2.4" rx=".6" fill="#ffffffb3"/><rect x="17" y="12.5" width="2.4" height="2.4" rx=".6" fill="#ffffffb3"/></svg>
  <span>Hold <kbd>&#x2325;</kbd> and press <kbd>S</kbd> repeatedly to quick-switch Spaces.</span>
  <button type="button" id="hintDismiss" aria-label="Dismiss hint">&#10005;</button>
</div>
<script src="app.js"></script></body></html>)HTML";

constexpr char kAppJs[] = R"JS(let refreshCounter = 0;
let entered = false;
let exiting = false;
// recon §8: hosted window-wide (?window=1) the page strips its header and
// card clicks exit the window-level mode instead of just focusing.
const windowMode = location.search.indexOf("window=1") !== -1;
if (windowMode) document.body.classList.add("window-mode");
function action(id, kind) {
  chrome.send("spaceAction", [id, kind]);
  setTimeout(refresh, 250);
}
window.prismSpaces = { onData, onShown };
function onShown() {
  entered = false;
  exiting = false;
  modeShownAt = Date.now();
  refreshCounter++;
  refresh();
}
function span(parent, className, text) {
  const el = document.createElement("span");
  if (className) el.className = className;
  if (text !== undefined) el.textContent = text;
  parent.appendChild(el);
  return el;
}

// ---- top-center "N Space(s) ⌄" caption + dropdown (tab-hosted page) ----
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
} catch (e) { hintBar.hidden = false; }
document.getElementById("hintDismiss").addEventListener("click", () => {
  hintBar.hidden = true;
  try { localStorage.setItem("prismSpacesHintDismissed", "1"); } catch (e) {}
});

// ---- recon §7/§8 enter motion (~0.5-0.7s ease-out) -----------------------
const header = document.querySelector("header");
function revealEnter(focusedCard) {
  if (!windowMode) header.classList.add("enter");
  hintBar.classList.add("enter");
  let d = 120;
  let pop = 650;
  for (const item of document.querySelectorAll("#wall .item")) {
    if (item.querySelector(".card") !== focusedCard) {
      item.style.setProperty("--d", d + "ms");
      item.classList.add("enter");
      d += 50;
    }
    const card = item.querySelector(".card");
    if (card && card.dataset.agent === "1") {
      const img = card.querySelector(".thumb img");
      if (img) {
        img.style.setProperty("--d", pop + "ms");
        img.classList.add("pop");
        pop += 55;
      }
    }
  }
  if (focusedCard && !focusedCard.classList.contains("fx-self-hidden")) {
    focusedCard.closest(".item").classList.add("enter");
  }
}
function runShrinkIn(card, overlayImg) {
  const thumb = card.querySelector(".thumb");
  const rect = thumb.getBoundingClientRect();
  const fx = document.createElement("img");
  fx.id = "fxOverlay";
  fx.src = overlayImg.src;
  Object.assign(fx.style, { left: "0px", top: "0px",
                            width: innerWidth + "px",
                            height: innerHeight + "px" });
  document.body.appendChild(fx);
  const dx = rect.left + rect.width / 2 - innerWidth / 2;
  const dy = rect.top + rect.height / 2 - innerHeight / 2;
  const sx = rect.width / innerWidth;
  const sy = rect.height / innerHeight;
  const pos = `translate(${dx}px, ${dy}px) scale(${sx}, ${sy})`;
  // recon §8: the content lifts ~5-10% of the window height, then scales
  // down into the card slot with a crossfade (~0.6-0.8s total ease-out).
  const lift = Math.round(innerHeight * -0.07);
  fx.animate([
    { transform: `translateY(${lift}px) scale(1)`, opacity: 1,
      borderRadius: "0px" },
    { transform: pos, opacity: 1, borderRadius: "14px", offset: 0.85 },
    { transform: pos, opacity: 0, borderRadius: "14px" }
  ], { duration: 650, easing: "cubic-bezier(.2,.8,.25,1)", fill: "forwards" })
    .onfinish = () => { fx.remove(); card.classList.remove("fx-self-hidden"); };
}
function playEnter(data) {
  const currentId = (data.current !== undefined) ? data.current : data.focused;
  const focusedCard = document.querySelector(
      `#wall .card[data-space="${currentId}"]`);
  // The dark dashboard backdrop fades in behind the shrink (recon §8).
  const backdrop = document.createElement("div");
  backdrop.id = "fxBackdrop";
  document.body.appendChild(backdrop);
  requestAnimationFrame(() => { backdrop.style.opacity = "1"; });
  setTimeout(() => backdrop.remove(), 1400);
  // The current card stays hidden until the shrink-in overlay lands in it;
  // the overlay fetch doubles as the on-demand thumbnail (900ms budget).
  if (focusedCard) focusedCard.classList.add("fx-self-hidden");
  revealEnter(focusedCard);
  const img = focusedCard && focusedCard.querySelector(".thumb img");
  if (!img) return;
  const pre = new Image();
  let done = false;
  const finish = (ok) => {
    if (done || exiting) return;
    done = true;
    if (ok) {
      runShrinkIn(focusedCard, pre);
    } else {
      focusedCard.classList.remove("fx-self-hidden");
      focusedCard.closest(".item").classList.add("enter");
    }
  };
  pre.onload = () => finish(true);
  pre.onerror = () => finish(false);
  pre.src = img.src;
  setTimeout(() => finish(false), 900);
}

// ---- recon §7/§8 exit motion: the clicked card scales back up into the page
function openSpace(card, id) {
  if (exiting) { action(id, windowMode ? "exitSpaces" : "focus"); return; }
  exiting = true;
  const thumb = card.querySelector(".thumb");
  const img = card.querySelector(".thumb img");
  const fadeTargets = [hintBar, ...document.querySelectorAll("#wall .item")];
  if (!windowMode) fadeTargets.push(header);
  for (const el of fadeTargets) {
    if (!el.contains(card)) {
      el.style.transition = "opacity .3s ease";
      el.style.opacity = "0";
    }
  }
  if (img && thumb) {
    const rect = thumb.getBoundingClientRect();
    const fx = document.createElement("img");
    fx.id = "fxOverlay";
    fx.src = img.src;
    Object.assign(fx.style, { left: rect.left + "px", top: rect.top + "px",
                              width: rect.width + "px",
                              height: rect.height + "px",
                              borderRadius: "14px" });
    document.body.appendChild(fx);
    const dx = innerWidth / 2 - (rect.left + rect.width / 2);
    const dy = innerHeight / 2 - (rect.top + rect.height / 2) + 12;
    const sx = innerWidth / rect.width;
    const sy = innerHeight / rect.height;
    const pos = `translate(${dx}px, ${dy}px) scale(${sx}, ${sy})`;
    fx.animate([
      { transform: "translate(0px, 0px) scale(1)", opacity: 1,
        borderRadius: "14px" },
      { transform: pos, opacity: 1, borderRadius: "0px", offset: 0.85 },
      { transform: pos, opacity: 0, borderRadius: "0px" }
    ], { duration: 550, easing: "cubic-bezier(.2,.8,.25,1)", fill: "forwards" })
      .onfinish = () => fx.remove();
  }
  // Fire as the expansion crosses fullscreen: window mode exits into the
  // space; the tab-hosted page just focuses it.
  setTimeout(() => action(id, windowMode ? "exitSpaces" : "focus"),
             windowMode ? 500 : 380);
}

let lastData = null;
function onData(payloadJson) {
  const data = JSON.parse(payloadJson);
  lastData = data;
  const spaces = data.spaces || [];
  document.getElementById("captionText").textContent =
      spaces.length + " Space" + (spaces.length === 1 ? "" : "s");
  buildCaptionMenu(spaces, data.focused);

  const wall = document.getElementById("wall");
  wall.replaceChildren();
  for (const space of spaces) {
    const item = document.createElement("div");
    item.className = "item";
    const card = document.createElement("div");
    card.className = "card" + (space.id === data.focused ? " focused" : "");
    // Automation contract (docs/binding-contract.md §wall): the space's
    // identity/state travels as data attributes, not visible chrome.
    card.dataset.space = space.id;
    card.dataset.name = space.name || "";
    card.dataset.ownership = space.ownership;
    card.dataset.state = space.agentTaskState || "";
    card.dataset.windowShown = space.windowShown ? "1" : "0";
    if (space.ownership === "agent") card.dataset.agent = "1";
    card.addEventListener("click", () => openSpace(card, space.id));

    const thumb = document.createElement("div");
    thumb.className = "thumb";
    if (space.hasTabs) {
      const img = document.createElement("img");
      img.src = "thumb/" + space.id + "." + refreshCounter + ".png";
      img.alt = "";
      thumb.appendChild(img);
    }
    card.appendChild(thumb);
    item.appendChild(card);

    // recon §7/§8: under the card — left "Space" label (agent cards: blue
    // "Running" chip + task name), right the dynamic watermark.
    const foot = document.createElement("div");
    foot.className = "cardfoot";
    const left = document.createElement("span");
    left.className = "foot-left";
    span(left, "space-label", "Space");
    if (space.ownership === "agent") {
      span(left, "chip running", "Running");
      span(left, "foot-task", space.name);
    }
    foot.appendChild(left);
    span(foot, "watermark", data.watermark || "Your Prism");
    item.appendChild(foot);
    wall.appendChild(item);
  }

  const addItem = document.createElement("div");
  addItem.className = "item";
  const add = document.createElement("div");
  add.className = "card newspace";
  const plus = document.createElement("span");
  plus.className = "plus";
  plus.textContent = "+";
  add.appendChild(plus);
  add.addEventListener("click", () => action(0, "create"));
  addItem.appendChild(add);
  wall.appendChild(addItem);

  if (windowMode && data.shownAt && data.shownAt !== lastShownAt) {
    lastShownAt = data.shownAt;
    entered = false;
    exiting = false;
    modeShownAt = Date.now();
  }
  if (!entered) {
    entered = true;
    if (!windowMode) modeShownAt = Date.now();
    playEnter(data);
  }
}
document.getElementById("deleteAll").addEventListener("click", () => {
  action(0, "deleteAll");
});
function refresh() {
  chrome.send("querySpaces");
}
// recon §8: re-shown in window mode (or re-entered) → replay the enter
// motion and pull fresh thumbnails.
document.addEventListener("visibilitychange", () => {
  if (!document.hidden && windowMode) {
    entered = false;
    exiting = false;
    modeShownAt = Date.now();
    refreshCounter++;
    refresh();
  }
});
// recon §8: the mode host re-registers with a fresh shownAt on every show —
// replay the enter motion whenever it changes (works on re-entry even when
// no visibilitychange reaches the page).
let lastShownAt = -1;
// recon §8: ⌘⇧S exits the mode. The mode host's WebView sits outside the
// widget's accelerator focus chain, so the native accelerator never fires
// while the wall has focus — handle the chord in-page instead (the native
// path still owns entry from normal chrome).
document.addEventListener("keydown", (e) => {
  if (windowMode && e.metaKey && e.shiftKey &&
      (e.key === "s" || e.key === "S")) {
    e.preventDefault();
    // The keypress that ENTERED the mode propagates to the wall too (the
    // native accelerator does not consume it) — ignore the chord during the
    // entry window so a single press does not enter and immediately exit.
    if (exiting || Date.now() - modeShownAt < 700) return;
    exiting = true;
    action(currentSpaceId(), "exitSpaces");
  }
});
function currentSpaceId() {
  return lastData && lastData.current !== undefined ? lastData.current : 0;
}
let modeShownAt = 0;
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

// Live controllers, for NotifyModeShown (recon §8).
std::set<PrismSpacesUI*>& SpacesUIRegistry() {
  static base::NoDestructor<std::set<PrismSpacesUI*>> registry;
  return *registry;
}

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

// Clipped DevTools captures resize the target's RenderWidgetHostView, which
// wedges a live tab (the default space card captures the main window's
// current page every poll). So thumbnails capture the viewport at native
// size and downscale in-process to 1/4 instead (recon §8).
std::string DownscalePng(const std::string& png) {
  const SkBitmap decoded = gfx::PNGCodec::Decode(
      base::as_bytes(base::span(png)));
  if (decoded.isNull()) {
    return png;
  }
  const int width = std::max(1, decoded.width() / 4);
  const int height = std::max(1, decoded.height() / 4);
  SkBitmap bitmap;
  if (!bitmap.tryAllocPixels(SkImageInfo::MakeN32Premul(width, height))) {
    return png;
  }
  SkCanvas canvas(bitmap);
  canvas.clear(SK_ColorTRANSPARENT);
  canvas.drawImageRect(decoded.asImage(), SkRect::MakeWH(width, height),
                       SkSamplingOptions(SkFilterMode::kLinear));
  auto encoded = gfx::PNGCodec::FastEncodeBGRASkBitmap(
      bitmap, /*discard_transparency=*/false);
  if (!encoded) {
    return png;
  }
  return std::string(encoded->begin(), encoded->end());
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
    // No clip: a clipped capture resizes the target's RenderWidgetHostView,
    // which wedges a visible tab (the default space card captures the main
    // window's current page every poll). Scale-only captures the current
    // viewport at 0.25 — windowless tabs keep their fixed 1280x800 viewport.
    const std::string message =
        R"({"id":1,"method":"Page.captureScreenshot","params":{"format":"png","scale":0.25}})";
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
      ThumbCache()[space_id_] = {DownscalePng(png), base::TimeTicks::Now()};
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
  if (space_id < 0) {
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

  scoped_refptr<content::DevToolsAgentHost> host;
  if (space_id == 0) {
    // recon §8: the implicit default space is a first-class wall card — its
    // thumbnail is the default browsing window's active tab.
    if (auto* delegate = GetPrismSpaceWindowDelegate()) {
      if (content::WebContents* active = delegate->ActiveTabForDefaultSpace()) {
        host = content::DevToolsAgentHost::GetOrCreateFor(active);
      }
    }
  } else {
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
    if (!target_id.empty()) {
      host = content::DevToolsAgentHost::GetForId(target_id);
    }
  }
  if (!host) {
    RespondPng(std::move(callback),
               std::string(reinterpret_cast<const char*>(kFallbackPng),
                           sizeof(kFallbackPng)));
    return;
  }

  auto* job = new ThumbnailJob(space_id, std::move(host), std::move(callback));
  job->Run();
}

// recon §7: the under-card watermark is "<account/display name> Agents
// Prism", with "Your Prism" as the signed-out fallback.
std::string Watermark(content::WebUI* web_ui) {
  std::string name;
  auto* profile =
      Profile::FromBrowserContext(web_ui->GetWebContents()->GetBrowserContext());
  if (profile && g_browser_process && g_browser_process->profile_manager()) {
    auto& storage =
        g_browser_process->profile_manager()->GetProfileAttributesStorage();
    if (const ProfileAttributesEntry* entry =
            storage.GetProfileAttributesWithPath(profile->GetPath())) {
      name = base::UTF16ToUTF8(entry->GetGAIAName());
    }
  }
  return name.empty() ? "Your Prism" : name + " Agents Prism";
}

// The space to highlight as "current" in the enter animation: the space
// whose window hosts the dashboard — via the tab strip when tab-hosted
// (automation), via the spaces-mode registry when hosted window-wide
// (recon §8).
int CurrentSpaceIdForWebContents(content::WebContents* wc) {
  auto* delegate = GetPrismSpaceWindowDelegate();
  if (!delegate) {
    return 0;
  }
  if (int id = delegate->SpaceIdForWebContents(wc)) {
    return id;
  }
  return delegate->SpaceIdForModeWebContents(wc);
}

std::string SpacesJson(const std::string& watermark, int current_space_id,
                       int64_t shown_at_ms) {
  auto* manager = SpaceManager::GetInstance();
  base::DictValue root;
  root.Set("focused", manager->focused_space_id());
  root.Set("current", current_space_id);
  root.Set("watermark", watermark);
  // recon §8: the mode's last-show timestamp drives the wall's enter replay
  // (0 when tab-hosted).
  root.Set("shownAt", static_cast<double>(shown_at_ms));
  base::ListValue spaces;
  // recon §8: the implicit default browsing context is a first-class wall
  // card (id 0), always first — counted in the caption like any space.
  {
    base::DictValue entry;
    entry.Set("id", 0);
    entry.Set("name", "Space");
    entry.Set("taskId", "");
    entry.Set("createdBy", "user");
    entry.Set("ownership", "user");
    entry.Set("agentTaskState", "");
    entry.Set("windowShown", true);
    bool has_tab = false;
    base::ListValue tabs;
    if (auto* delegate = GetPrismSpaceWindowDelegate()) {
      if (content::WebContents* active = delegate->ActiveTabForDefaultSpace()) {
        has_tab = true;
        base::DictValue t;
        t.Set("title", active->GetTitle());
        t.Set("url", active->GetLastCommittedURL().spec());
        t.Set("active", true);
        tabs.Append(std::move(t));
      }
    }
    entry.Set("hasTabs", has_tab);
    entry.Set("tabs", std::move(tabs));
    spaces.Append(std::move(entry));
  }
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
  SpacesUIRegistry().insert(this);
}

PrismSpacesUI::~PrismSpacesUI() {
  SpacesUIRegistry().erase(this);
}

// static
void PrismSpacesUI::NotifyModeShown(content::WebContents* wc) {
  for (auto* ui : SpacesUIRegistry()) {
    if (ui->web_ui()->GetWebContents() == wc) {
      // Push fresh data and the show signal through the controller's own
      // channel — plain CallJavascriptFunctionUnsafe from an arbitrary task
      // does not reach this WebContents (recon §8 investigation).
      ui->OnQuerySpaces(base::ListValue());
      ui->web_ui()->CallJavascriptFunctionUnsafe("prismSpaces.onShown",
                                                 base::ValueView(base::Value(0)));
      return;
    }
  }
}

void PrismSpacesUI::OnQuerySpaces(const base::ListValue& args) {
  // The page is our own (served from the request filter above), so the
  // lifecycle concern behind CallJavascriptFunctionUnsafe's name does not
  // apply beyond startup — the page only queries after it is interactive.
  web_ui()->CallJavascriptFunctionUnsafe("prismSpaces.onData",
                                         base::ValueView(SpacesJson(
          Watermark(web_ui()),
          CurrentSpaceIdForWebContents(web_ui()->GetWebContents()),
          GetPrismSpaceWindowDelegate()
              ? GetPrismSpaceWindowDelegate()
                    ->SpacesModeShownAt(web_ui()->GetWebContents())
                    .ToDeltaSinceWindowsEpoch()
                    .InMilliseconds()
              : 0)));
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
  } else if (action == "exitSpaces") {
    // recon §8: card click inside the window-level dashboard — restore the
    // normal chrome and open the chosen space (recon §8). No-op when the
    // page is tab-hosted (automation keeps using "focus").
    if (auto* delegate = GetPrismSpaceWindowDelegate();
        delegate && delegate->IsSpacesModeWebContents(
                        web_ui()->GetWebContents())) {
      delegate->ExitSpacesMode(web_ui()->GetWebContents(), id);
      return;
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
                                         base::ValueView(SpacesJson(
          Watermark(web_ui()),
          CurrentSpaceIdForWebContents(web_ui()->GetWebContents()),
          GetPrismSpaceWindowDelegate()
              ? GetPrismSpaceWindowDelegate()
                    ->SpacesModeShownAt(web_ui()->GetWebContents())
                    .ToDeltaSinceWindowsEpoch()
                    .InMilliseconds()
              : 0)));
}

}  // namespace prism
