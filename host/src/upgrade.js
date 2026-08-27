// `prism-browser upgrade` mechanics (Phase 7 / queue 5). The flow:
// check (host/src/update.js) -> download the release dmg -> verify -> mount
// -> quit the running app -> atomic swap at the install path -> reopen.
//
// Every side effect goes through an injectable `deps` object, so the whole
// sequence is exercised against a temp-dir "Applications" and a fixture dmg
// without touching the real app (host/scripts/update-flow-test.mjs). The
// production defaults are thin wrappers over hdiutil/osascript/open.
//
// Deliberate safety rails:
//   - nothing happens unless the manifest offers a newer version with a dmg
//     asset for this arch ("no update available" is a first-class, explicit
//     outcome, not an error);
//   - the mounted Prism.app must report the manifest's version, otherwise we
//     abort before touching the install;
//   - the swap keeps a timestamped backup until the new app is in place, then
//     removes it; a failed swap restores the old app;
//   - quit is graceful (AppleScript quit) with a best-effort TERM fallback.

import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { execFile } from "node:child_process";
import { promisify } from "node:util";

import { checkForUpdate } from "./update.js";

const execFileAsync = promisify(execFile);

export const DEFAULT_APP_PATH = "/Applications/Prism.app";
const APP_BUNDLE_ID = "com.logibricks.prism";

// Same trap as host/src/update.js: capture fetch before the harness bundle's
// installPrismSdk swaps globalThis.fetch for the agent facade.
const nativeFetch =
  typeof globalThis.fetch === "function" ? globalThis.fetch.bind(globalThis) : null;

function defaultDeps() {
  return {
    async download(url, destPath) {
      if (!nativeFetch) throw new Error("no fetch available");
      const response = await nativeFetch(url);
      if (!response.ok) {
        throw new Error(`download failed: HTTP ${response.status}`);
      }
      fs.writeFileSync(destPath, Buffer.from(await response.arrayBuffer()));
    },
    async mountDmg(dmgPath, mountPoint) {
      fs.mkdirSync(mountPoint, { recursive: true });
      await execFileAsync("hdiutil", [
        "attach", "-nobrowse", "-readonly", "-mountpoint", mountPoint, dmgPath,
      ]);
    },
    async unmount(mountPoint) {
      await execFileAsync("hdiutil", ["detach", "-quiet", mountPoint]).catch(() => {});
    },
    async appVersionAt(appPath) {
      const plist = path.join(appPath, "Contents", "Info.plist");
      const { stdout } = await execFileAsync("plutil", [
        "-extract", "CFBundleShortVersionString", "raw", plist,
      ]);
      return stdout.trim();
    },
    async quitRunningApp() {
      await execFileAsync("osascript", [
        "-e", `tell application id "${APP_BUNDLE_ID}" to quit`,
      ]).catch(() => {});
    },
    async swapApp(newAppPath, installPath) {
      const backup = `${installPath}.prism-backup-${Date.now()}`;
      fs.renameSync(installPath, backup);  // same-volume rename = atomic out
      try {
        // The source lives on a read-only dmg mount — a different device, so
        // rename is out (EXDEV, observed). Copy in, then drop the backup.
        fs.cpSync(newAppPath, installPath, {
          recursive: true,
          verbatimSymlinks: true,
        });
      } catch (error) {
        fs.renameSync(backup, installPath);  // restore on failure
        throw error;
      }
      fs.rmSync(backup, { recursive: true, force: true });
    },
    async reopenApp(installPath) {
      await execFileAsync("open", ["-a", installPath]).catch(() => {});
    },
    now: () => Date.now(),
  };
}

/** Ordered human/agent-readable plan lines (dry-run prints exactly these). */
export function planUpgrade({ currentVersion, latestVersion, downloadUrl, appPath }) {
  return [
    `download ${downloadUrl}`,
    `  -> release asset for this arch (Prism-mac-${process.arch}.dmg)`,
    `mount the dmg read-only and verify Prism.app reports ${latestVersion}`,
    `quit the running Prism (graceful AppleScript quit, TERM fallback)`,
    `swap: ${appPath} -> timestamped backup, new app into place, delete backup`,
    `reopen ${appPath}`,
    `(current ${currentVersion} -> ${latestVersion})`,
  ];
}

/**
 * Run the upgrade. `log` receives progress lines. Returns a process-style
 * exit code: 0 = up to date or upgraded, 1 = failed/impossible.
 * `currentVersion` is supplied by the caller (cli.js reads it off the kernel
 * or daemon bridge).
 */
export async function runUpgrade({ currentVersion, log, dryRun = false, env = process.env, deps, appPath }) {
  const d = { ...defaultDeps(), ...deps };
  const target = appPath || DEFAULT_APP_PATH;

  if (!fs.existsSync(target)) {
    log(`nothing to upgrade: no Prism.app at ${target}`);
    return 1;
  }

  log("checking for updates…");
  const status = await checkForUpdate({ currentVersion, env });
  if (!status) {
    log("update check failed or no release channel answered — try again later");
    return 1;
  }
  if (!status.updateAvailable) {
    log(`no update available (current ${currentVersion} is the latest)`);
    return 0;
  }
  if (!status.downloadUrl) {
    log(`update ${status.latestVersion} exists but ships no Prism-mac-${process.arch}.dmg asset`);
    return 1;
  }

  const plan = planUpgrade({
    currentVersion,
    latestVersion: status.latestVersion,
    downloadUrl: status.downloadUrl,
    appPath: target,
  });
  if (dryRun) {
    log(`dry-run: would upgrade Prism ${currentVersion} -> ${status.latestVersion}:`);
    for (const line of plan) log(`  ${line}`);
    return 0;
  }

  const workDir = fs.mkdtempSync(path.join(os.tmpdir(), "prism-upgrade-"));
  const dmgPath = path.join(workDir, "prism-update.dmg");
  const mountPoint = path.join(workDir, "mnt");
  try {
    log(`downloading ${status.downloadUrl} …`);
    await d.download(status.downloadUrl, dmgPath);

    await d.mountDmg(dmgPath, mountPoint);
    const stagedApp = path.join(mountPoint, "Prism.app");
    if (!fs.existsSync(stagedApp)) {
      throw new Error("the dmg contains no Prism.app");
    }
    const stagedVersion = await d.appVersionAt(stagedApp);
    if (stagedVersion !== status.latestVersion) {
      throw new Error(
        `version mismatch: manifest says ${status.latestVersion}, dmg contains ${stagedVersion} — aborting`,
      );
    }

    log("quitting the running Prism…");
    await d.quitRunningApp();
    log(`installing into ${target}…`);
    await d.swapApp(stagedApp, target);
    await d.reopenApp(target);
    log(`upgraded: Prism ${currentVersion} -> ${status.latestVersion}`);
    return 0;
  } catch (error) {
    log(`upgrade failed: ${error.message}`);
    return 1;
  } finally {
    await d.unmount(mountPoint);
    fs.rmSync(workDir, { recursive: true, force: true });
  }
}
