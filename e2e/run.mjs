#!/usr/bin/env node
// Repo-root entry for the real-browser E2E acceptance suite.
//
// The suite itself (cases, runner, fixture server) lives vendored in
// package/prism-browser/scripts/real-browser-e2e/. This entry point only:
//   1. seeds the default skip list for capabilities the prism-host adapter
//      does not provide on stock Chromium (with reasons), unless the caller
//      overrides PRISM_BROWSER_REAL_E2E_SKIP themselves, and
//   2. delegates to runRealBrowserE2e().
//
// Every case executes through our own stack: the runner spawns
// `node host/src/cli.js` per case, which auto-launches the prism-host daemon
// (unix socket under $TMPDIR) that drives stock Chrome over
// --remote-debugging-pipe with a dedicated dev profile.
//
// Usage:
//   node e2e/run.mjs
//
// Useful env knobs (see e2e/README.md):
//   PRISM_BROWSER_REAL_E2E_ONLY="environment initialization,wait helpers"
//   PRISM_BROWSER_REAL_E2E_SKIP="download helpers"
//   PRISM_BROWSER_REAL_E2E_VERBOSE_CASE_OUTPUT=1

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const repoRoot = path.join(path.dirname(fileURLToPath(import.meta.url)), "..");

// Case name -> reason. Reasons are surfaced in the runner output and the
// per-case summary; keep them specific and honest.
const DEFAULT_SKIPS = new Map();

// "screencast recording" pipes Page.startScreencast frames into an ffmpeg
// subprocess (harness video-recorder) and verifies the WebM with ffprobe.
// Skip only when the binaries are actually unavailable.
if (!ffmpegAvailable()) {
  DEFAULT_SKIPS.set(
    "screencast recording",
    "requires ffmpeg/ffprobe (harness pipes screencast frames into ffmpeg); set PRISM_BROWSER_FFMPEG_PATH/PRISM_BROWSER_FFPROBE_PATH or install ffmpeg to enable",
  );
}

// "macOS bare Meta input isolation" runs by default on the fork: patch 0013
// swallows DevTools-synthetic bare-Meta events before the shell redispatches
// them into [NSApp sendEvent:] (the macOS shortcut layer). The case fronts
// the space window itself via Prism.showTaskSpace so the probe is
// deterministic rather than focus-luck.

function binaryOnPath(name) {
  for (const dir of (process.env.PATH || "").split(path.delimiter)) {
    if (!dir) continue;
    try {
      fs.accessSync(path.join(dir, name), fs.constants.X_OK);
      return true;
    } catch {
      // keep looking
    }
  }
  return false;
}

function ffmpegAvailable() {
  const ffmpeg =
    process.env.PRISM_BROWSER_FFMPEG_PATH &&
    fs.existsSync(process.env.PRISM_BROWSER_FFMPEG_PATH);
  const ffprobe =
    process.env.PRISM_BROWSER_FFPROBE_PATH &&
    fs.existsSync(process.env.PRISM_BROWSER_FFPROBE_PATH);
  return Boolean(
    (ffmpeg || binaryOnPath("ffmpeg")) && (ffprobe || binaryOnPath("ffprobe")),
  );
}

// Caller-provided skip lists win; the defaults only fill in when the caller
// did not set one. Reasons are merged (caller reasons take precedence).
if (!process.env.PRISM_BROWSER_REAL_E2E_SKIP && DEFAULT_SKIPS.size > 0) {
  process.env.PRISM_BROWSER_REAL_E2E_SKIP = [...DEFAULT_SKIPS.keys()].join(",");
}
let callerReasons = {};
if (process.env.PRISM_BROWSER_REAL_E2E_SKIP_REASONS) {
  try {
    callerReasons = JSON.parse(process.env.PRISM_BROWSER_REAL_E2E_SKIP_REASONS);
  } catch {
    // The runner validates and reports malformed JSON itself.
  }
}
const mergedReasons = { ...Object.fromEntries(DEFAULT_SKIPS), ...callerReasons };
if (Object.keys(mergedReasons).length > 0) {
  process.env.PRISM_BROWSER_REAL_E2E_SKIP_REASONS =
    JSON.stringify(mergedReasons);
}

const activeSkips = (process.env.PRISM_BROWSER_REAL_E2E_SKIP || "")
  .split(",")
  .map((name) => name.trim())
  .filter(Boolean);
if (activeSkips.length > 0) {
  console.log("== Default skips (host adapter on stock Chromium) ==");
  for (const name of activeSkips) {
    console.log(`  ${name}: ${mergedReasons[name] || "(no reason recorded)"}`);
  }
  console.log("");
}

const runnerUrl = pathToFileURL(
  path.join(
    repoRoot,
    "package",
    "prism-browser",
    "scripts",
    "real-browser-e2e",
    "runner.mjs",
  ),
).href;
const { runRealBrowserE2e } = await import(runnerUrl);
await runRealBrowserE2e();
