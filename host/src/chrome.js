// Locate and spawn the browser process with a DevTools pipe.
//
// Browser resolution order:
//   1. PRISM_BROWSER_PATH env (explicit override)
//   2. Prism.app (our fork, once it ships)
//   3. Chromium.app / Google Chrome.app (Phase 1 development against stock builds)
//
// The browser always runs with a dedicated profile directory (never the user's
// default Chrome profile) so development runs cannot touch real browsing data.
// Login-state inheritance from the user's main profile is fork-era behavior.

import { spawn } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";

const APP_CANDIDATES = [
  "/Applications/Prism.app/Contents/MacOS/Prism",
  `${os.homedir()}/Applications/Prism.app/Contents/MacOS/Prism`,
  "/Applications/Chromium.app/Contents/MacOS/Chromium",
  `${os.homedir()}/Applications/Chromium.app/Contents/MacOS/Chromium`,
  "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
];

export function resolveBrowserPath(env = process.env) {
  if (env.PRISM_BROWSER_PATH && fs.existsSync(env.PRISM_BROWSER_PATH)) {
    return env.PRISM_BROWSER_PATH;
  }
  for (const candidate of APP_CANDIDATES) {
    if (fs.existsSync(candidate)) return candidate;
  }
  return null;
}

export function defaultProfileDir() {
  return (
    process.env.PRISM_BROWSER_PROFILE ||
    path.join(os.homedir(), "Library", "Application Support", "Prism", "dev-profile")
  );
}

// Spawns the browser with --remote-debugging-pipe. Returns the child process;
// the caller picks up the transport ends as child.stdio[3] (write) and
// child.stdio[4] (read).
//
// With usePipe:false the browser is spawned detached without the pipe switch:
// the kernel transport then talks to its agent socket (the fork starts the
// listener unconditionally). Detached + unref'd so the browser outlives a
// short-lived CLI process and can serve later clients.
//
// With useDefaultProfile:true (bundled CLI launching its own app) no
// --user-data-dir and no positional URL are passed: the app keeps its real
// profile (where the global agent socket lives) and its normal startup tabs.
export function spawnBrowser({ browserPath, profileDir, extraArgs = [], usePipe = true, useDefaultProfile = false }) {
  const args = [...extraArgs];
  if (!useDefaultProfile) {
    fs.mkdirSync(profileDir, { recursive: true });
    args.unshift(
      `--user-data-dir=${profileDir}`,
      "--no-first-run",
      "--no-default-browser-check",
      "--disable-session-crashed-bubble",
      "--hide-crash-restore-bubble",
    );
    args.push("about:blank");
  }
  if (usePipe) {
    args.unshift("--remote-debugging-pipe");
    return spawn(browserPath, args, {
      // fd 3 = our commands into the browser, fd 4 = browser messages back.
      stdio: ["ignore", "ignore", "inherit", "pipe", "pipe"],
    });
  }
  const child = spawn(browserPath, args, { detached: true, stdio: "ignore" });
  child.unref();
  return child;
}
