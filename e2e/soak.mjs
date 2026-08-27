#!/usr/bin/env node
// Soak: mixed-operation loop against the fork (task spaces, navigation,
// snapshots, clicks, screencast start/stop). Reports failure rate and
// browser RSS growth.
//
//   node e2e/soak.mjs [rounds=200] [maxMinutes=30]
//
// Env: PRISM_BROWSER_PATH (defaults to the fork build output).
// Results: e2e/results/soak-<timestamp>.json

import { spawn } from "node:child_process";
import { execFile } from "node:child_process";
import { promisify } from "node:util";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const REPO_ROOT = path.join(path.dirname(fileURLToPath(import.meta.url)), "..");
const hostSrc = path.join(REPO_ROOT, "host", "src");
const { spawnBrowser } = await import(path.join(hostSrc, "chrome.js"));
const { PipeTransport } = await import(path.join(hostSrc, "pipe.js"));
const { CdpConnection } = await import(path.join(hostSrc, "cdp.js"));
const execFileP = promisify(execFile);

const ROUNDS = Number(process.argv[2] || 200);
const MAX_MINUTES = Number(process.argv[3] || 30);
const browserPath =
  process.env.PRISM_BROWSER_PATH ||
  path.join(os.homedir(),
    "chromium/prism/src/out/Prism-arm64/Prism.app/Contents/MacOS/Prism");

const profileDir = path.join(os.tmpdir(), `prism-soak-${Date.now()}`);
const child = spawnBrowser({ browserPath, profileDir });
const browserPid = child.pid;
const cdp = new CdpConnection(new PipeTransport({
  writeStream: child.stdio[3],
  readStream: child.stdio[4],
}));

const startedAt = Date.now();
const deadline = startedAt + MAX_MINUTES * 60_000;
const stats = {
  roundsDone: 0, failures: 0, failureKinds: {}, rssSamples: [],
};

async function rssKb(pid) {
  try {
    const { stdout } = await execFileP("ps", ["-o", "rss=", "-p", String(pid)]);
    return Number(stdout.trim()) || 0;
  } catch {
    return 0;
  }
}

async function timed(fn, timeoutMs, label) {
  let timer;
  try {
    return await Promise.race([
      fn(),
      new Promise((_, rej) => {
        timer = setTimeout(() => rej(new Error(`${label} timeout`)), timeoutMs);
      }),
    ]);
  } finally {
    clearTimeout(timer);
  }
}

async function soakRound(i) {
  const space = await cdp.send("Prism.createTaskSpace", { name: `soak-${i}` });
  const spaceId = space.taskSpace.id;
  await cdp.send("Prism.useTaskSpace", { id: spaceId });
  const { targetId } = await cdp.send("Prism.createTab",
    { url: "https://example.com" });
  const { sessionId } = await cdp.send("Target.attachToTarget",
    { targetId, flatten: true });
  await cdp.send("Runtime.evaluate",
    { expression: "document.title", returnByValue: true }, sessionId);
  const snap = await cdp.send("Prism.snapshot", {});
  if (!snap.content) throw new Error("empty snapshot");
  await cdp.send("Input.dispatchMouseEvent",
    { type: "mouseMoved", x: 100, y: 100 }, sessionId);
  if (i % 10 === 0) {
    // screencast start/stop smoke
    await cdp.send("Page.startScreencast",
      { format: "webm", quality: 30, maxWidth: 640, maxHeight: 400 },
      sessionId).catch(() => {});
    await cdp.send("Page.stopScreencast", {}, sessionId).catch(() => {});
  }
  await cdp.send("Prism.closeTaskSpace");
  stats.rssSamples.push(await rssKb(browserPid));
}

console.log(`soak: up to ${ROUNDS} rounds / ${MAX_MINUTES} min on ${browserPath}`);
const firstRss = await rssKb(browserPid);

let round = 0;
try {
  while (round < ROUNDS && Date.now() < deadline) {
    round++;
    try {
      await timed(() => soakRound(round), 45_000, `round ${round}`);
      stats.roundsDone = round;
      if (round % 10 === 0) {
        console.log(`round ${round} ok (rss=${await rssKb(browserPid)}KB)`);
      }
    } catch (error) {
      stats.failures++;
      const kind = (error.message || "error").slice(0, 60);
      stats.failureKinds[kind] = (stats.failureKinds[kind] || 0) + 1;
      console.log(`round ${round} FAIL: ${kind}`);
      // reconnect: a broken pipe makes every later round meaningless
      if (/pipe closed|disconnected/i.test(error.message)) {
        console.log("transport died; stopping early");
        break;
      }
    }
  }
} finally {
  const lastRss = await rssKb(browserPid);
  const result = {
    browser: browserPath,
    startedAt: new Date(startedAt).toISOString(),
    roundsPlanned: ROUNDS,
    maxMinutes: MAX_MINUTES,
    roundsDone: stats.roundsDone,
    failures: stats.failures,
    failureKinds: stats.failureKinds,
    rssStartKb: firstRss,
    rssEndKb: lastRss,
    rssPeakKb: Math.max(firstRss, lastRss, ...stats.rssSamples),
    rssSamples: stats.rssSamples,
  };
  const outDir = path.join(REPO_ROOT, "e2e", "results");
  fs.mkdirSync(outDir, { recursive: true });
  const out = path.join(outDir, `soak-${Date.now()}.json`);
  fs.writeFileSync(out, JSON.stringify(result, null, 2) + "\n");
  console.log("SOAK RESULT", JSON.stringify(result, null, 2));
  console.log(`written: ${out}`);
  child.kill("SIGTERM");
  process.exit(stats.failures > 0 && stats.roundsDone === 0 ? 1 : 0);
}
