#!/usr/bin/env node
// Spaces-mode crash stress (macOS): hammers the window-level spaces mode and
// the space-window lifecycle to prove no use-after-free survives in the
// WebContents/window registries (field SIGSEGV at 0xcdcdcdcd in
// PrismSpaceWindowDelegate::SpaceIdForWebContents, poll-driven).
//
// Each round:
//   1. enter the mode (View > Spaces Overview), wait for the mode wall
//   2. click a card (exit+focus path: ExitSpacesMode -> ShowTaskSpace)
//   3. re-enter the mode, poll querySpaces
//   4. close the space window, then immediately spam querySpaces from the
//      tab-hosted wall — a poll landing before the 1s reconcile tick walks
//      the pruned windows_ registry (the original crash), so this must not
//      fault; reshow the space window afterwards
//   5. rapid enter/exit toggles
//   6. assert the browser is still alive
//
// Usage:
//   node e2e/spaces-mode-stress.mjs --browser /path/to/Prism --rounds 10
//
// The browser is launched with a throwaway profile offscreen; nothing is
// left running on success. macOS only (drives the View menu via System
// Events).

import { spawn, execFileSync } from "node:child_process";
import fs from "node:fs";
import net from "node:net";
import os from "node:os";
import path from "node:path";

const args = process.argv.slice(2);
function opt(name, fallback) {
  const i = args.indexOf("--" + name);
  return i === -1 ? fallback : args[i + 1];
}
const BROWSER = opt("browser", null);
const ROUNDS = Number(opt("rounds", "10"));
if (!BROWSER || !fs.existsSync(BROWSER)) {
  console.error("usage: node e2e/spaces-mode-stress.mjs --browser <path> [--rounds N]");
  process.exit(2);
}

const UD = fs.mkdtempSync(path.join(os.tmpdir(), "prism-stress-ud-"));
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let phase = "launch";

// ---------------------------------------------------------------- browser --
const child = spawn(BROWSER, [
  "--user-data-dir=" + UD,
  "--no-first-run",
  "--no-default-browser-check",
  "--disable-session-crashed-bubble",
  "--hide-crash-restore-bubble",
  "--window-position=3000,3000",
  "--window-size=1440,900",
  "--remote-debugging-port=0",
], { stdio: ["ignore", "ignore", "inherit"] });

function alive() {
  try {
    process.kill(child.pid, 0);
    return true;
  } catch {
    return false;
  }
}
function die(msg) {
  console.error(`CRASH/FAIL at phase [${phase}]: ${msg}`);
  try { process.kill(child.pid, "SIGKILL"); } catch {}
  process.exit(1);
}

for (let i = 0; i < 60 && !fs.existsSync(path.join(UD, "DevToolsActivePort")); i++) {
  if (!alive()) die("browser exited during launch");
  await sleep(250);
}
const PORT = fs.readFileSync(path.join(UD, "DevToolsActivePort"), "utf8").split("\n")[0].trim();

// -------------------------------------------------------------------- CDP --
const version = await (await fetch(`http://127.0.0.1:${PORT}/json/version`)).json();
const ws = new WebSocket(version.webSocketDebuggerUrl);
let nextId = 0;
const pending = new Map();
ws.onmessage = (ev) => {
  const m = JSON.parse(ev.data);
  if (m.id && pending.has(m.id)) {
    const { res, rej } = pending.get(m.id);
    pending.delete(m.id);
    m.error ? rej(new Error(JSON.stringify(m.error))) : res(m.result);
  }
};
await new Promise((r) => (ws.onopen = r));
ws.onclose = () => console.error("WS CLOSED (browser endpoint gone) at phase:", phase);
const cdp = (method, params = {}, sessionId) =>
  new Promise((res, rej) => {
    const id = ++nextId;
    pending.set(id, { res, rej });
    ws.send(JSON.stringify({ id, method, params, ...(sessionId ? { sessionId } : {}) }));
  });
const pages = () => fetch(`http://127.0.0.1:${PORT}/json/list`).then((r) => r.json());
async function evalIn(sessionId, expr) {
  const r = await cdp("Runtime.evaluate", { expression: expr, returnByValue: true }, sessionId);
  return r.result && r.result.value;
}

// ------------------------------------------------------------------ socket --
const sockConn = net.createConnection(path.join(UD, "prism-agent.sock"));
let sockBuf = Buffer.alloc(0);
let sockId = 0;
const sockPending = new Map();
sockConn.on("data", (chunk) => {
  sockBuf = Buffer.concat([sockBuf, chunk]);
  let idx;
  while ((idx = sockBuf.indexOf(0)) !== -1) {
    const msg = JSON.parse(sockBuf.subarray(0, idx).toString());
    sockBuf = sockBuf.subarray(idx + 1);
    if (msg.id && sockPending.has(msg.id)) {
      const { res, rej } = sockPending.get(msg.id);
      sockPending.delete(msg.id);
      msg.error ? rej(new Error(JSON.stringify(msg.error))) : res(msg.result);
    }
  }
});
await new Promise((r) => sockConn.on("connect", r));
const sock = (method, params = {}) =>
  new Promise((res, rej) => {
    const id = ++sockId;
    sockPending.set(id, { res, rej });
    sockConn.write(JSON.stringify({ id, method, params }) + "\0");
  });

// ----------------------------------------------------------------- helpers --
function osascript(script) {
  try {
    execFileSync("/usr/bin/osascript", ["-e", script], { stdio: "pipe" });
    return true;
  } catch {
    return false;
  }
}
function windowList() {
  try {
    return execFileSync("/usr/bin/osascript", ["-e", 'tell application "System Events" to get name of every window of (every process whose name is "Prism")'], { stdio: ["pipe", "pipe", "pipe"] }).toString().trim();
  } catch (e) { return "<err>"; }
}
function toggleMode() {
  // View > Spaces Overview toggles the window-level spaces mode.
  return osascript(
    'tell application "System Events" to tell process "Prism" to click menu item "Spaces Overview" of menu 1 of menu bar item "View" of menu bar 1',
  );
}

async function findWall(visibleOnly = true) {
  for (const t of await pages()) {
    if (t.type !== "page" || !t.url.includes("prism-spaces") || !t.url.includes("window=1")) continue;
    const { sessionId } = await cdp("Target.attachToTarget", { targetId: t.id, flatten: true });
    const vis = await evalIn(sessionId, "document.visibilityState");
    if (!visibleOnly || vis === "visible") return sessionId;
  }
  return null;
}

async function ensureModeEntered() {
  // State-aware: dropped AX toggles desync the open/closed parity, so check
  // the real state before every toggle instead of assuming it. AX menu
  // clicks are best-effort on a background app; the final retry activates
  // the app (we are stress-testing the browser, not the AX layer).
  for (let attempt = 1; attempt <= 3; attempt++) {
    const already = await findWall(true);
    if (already) return already;
    if (attempt === 3) {
      osascript('tell application "Prism" to activate');
      await sleep(600);
    }
    toggleMode();
    const wall = await waitWall(true, 5000);
    if (wall) return wall;
  }
  return null;
}

async function waitWall(wantVisible, timeoutMs = 6000) {
  const start = Date.now();
  while (Date.now() - start < timeoutMs) {
    if (!alive()) die("browser gone");
    const session = await findWall(wantVisible);
    if (session) return session;
    await sleep(200);
  }
  return null;
}

async function spamQueries(n, gapMs) {
  // Tab-hosted wall: its OnQuerySpaces runs SpaceIdForWebContents every time.
  const targets = await pages();
  const tab = targets.find((t) => t.type === "page" && t.url.startsWith("chrome://prism-spaces") && !t.url.includes("window=1"));
  if (!tab) return false;
  const { sessionId } = await cdp("Target.attachToTarget", { targetId: tab.id, flatten: true });
  for (let i = 0; i < n; i++) {
    if (!alive()) die("browser gone mid-spam");
    await evalIn(sessionId, 'chrome.send("querySpaces"); 1');
    await sleep(gapMs);
  }
  return true;
}

async function closeSpaceWindow() {
  const targets = await pages();
  const repro = targets.find((t) => t.type === "page" && t.url.includes("402748"));
  if (!repro) return 0;
  const { windowId } = await cdp("Browser.getWindowForTarget", { targetId: repro.id });
  let closed = 0;
  for (const t of targets.filter((t) => t.type === "page")) {
    try {
      const w = await cdp("Browser.getWindowForTarget", { targetId: t.id });
      if (w.windowId === windowId) {
        await cdp("Target.closeTarget", { targetId: t.id });
        closed++;
      }
    } catch { /* already gone */ }
  }
  return closed;
}

// ------------------------------------------------------------------- setup --
phase = "setup: spaces tab";
await cdp("Target.createTarget", { url: "chrome://prism-spaces/", newWindow: false });

phase = "setup: agent space";
const created = await sock("Prism.createTaskSpace", { name: "stress-space" });
const spaceId = created.taskSpace.id;
await sock("Prism.useTaskSpace", { id: spaceId });
await sock("Prism.claimTaskSpace", { id: spaceId });
const html = "data:text/html," + encodeURIComponent(
  '<body style="margin:0;background:#402748;color:white;font:600 64px -apple-system;display:flex;align-items:center;justify-content:center;height:100vh">REPRO</body>');
await sock("Prism.createTab", { url: html });
await sock("Prism.showTaskSpace", { id: spaceId });
await sleep(1200);

// ------------------------------------------------------------------ rounds --
for (let round = 1; round <= ROUNDS; round++) {
  phase = `round ${round}: enter mode`;
  if (!alive()) die("browser gone before round");
  const wall = await ensureModeEntered();
  if (!wall) die("mode wall did not appear");

  phase = `round ${round}: click card (exit+focus)`;
  await evalIn(wall, `(() => { const c = document.querySelector('#wall .card[data-space="${spaceId}"]') || document.querySelector("#wall .card"); if (c) { c.click(); return 1; } return 0; })()`);
  await sleep(900);
  if (!alive()) die("browser gone after card click");

  phase = `round ${round}: re-enter mode`;
  const wall2 = await ensureModeEntered();
  if (!wall2) die("mode wall did not reappear");
  await spamQueries(3, 120);

  phase = `round ${round}: close space window + poll race`;
  console.log("  before close:", windowList());
  await closeSpaceWindow();
  console.log("  after close:", windowList(), "alive:", alive());
  await spamQueries(10, 90);  // lands before the 1s reconcile tick
  if (!alive()) die("browser gone after window-close poll race");
  await sock("Prism.showTaskSpace", { id: spaceId });
  await sock("Prism.createTab", { url: html });  // the close removed it
  await sleep(900);

  phase = `round ${round}: rapid enter/exit`;
  for (let i = 0; i < 3; i++) {
    toggleMode();
    await sleep(400);
    if (!alive()) die("browser gone during rapid toggles");
  }
  // End the round with the mode closed: if a wall is up, exit via card.
  const openWall = await findWall(true);
  if (openWall) {
    await evalIn(openWall, `document.querySelector("#wall .card")?.click(); 1`);
    await sleep(700);
  }
  if (!alive()) die("browser gone at round end");
  console.log(`round ${round}/${ROUNDS} PASS`);
}

phase = "done";
console.log(`STRESS OK: ${ROUNDS} rounds, browser alive`);
try { process.kill(child.pid, "SIGTERM"); } catch {}
await sleep(500);
try { process.kill(child.pid, "SIGKILL"); } catch {}
fs.rmSync(UD, { recursive: true, force: true });
process.exit(0);
