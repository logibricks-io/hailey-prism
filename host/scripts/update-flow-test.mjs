// Update-flow test against a loopback "releases" mock (Phase 7 / queue 5).
// Covers: semver compare, manifest parsing, the 6h write-behind cache,
// failure silence (404/timeout), the SDK/CLI banner line, `upgrade
// --dry-run`, "already latest", and the real swap mechanics against a
// fixture dmg + a temp-dir app (the real /Applications is never touched).
//
// Run: node host/scripts/update-flow-test.mjs   (from the repo root)

import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import http from "node:http";
import { execFileSync } from "node:child_process";
import { promisify } from "node:util";
import { fileURLToPath } from "node:url";

const hostSrc = path.join(path.dirname(fileURLToPath(import.meta.url)), "..", "src");
const update = await import(path.join(hostSrc, "update.js"));
const upgrade = await import(path.join(hostSrc, "upgrade.js"));

let failures = 0;
function check(label, cond, detail = "") {
  console.log(`${cond ? "PASS" : "FAIL"}  ${label}` + (cond ? "" : ` — ${detail}`));
  if (!cond) failures++;
}

// ---- unit: version comparison + manifest parsing ----
check("semver: older is detected",
  update.compareVersions("Prism/151.0.7922.174", "151.0.7922.999") < 0);
check("semver: same is equal (v-prefix tolerated)",
  update.compareVersions("151.0.7922.174", "v151.0.7922.174") === 0);
check("semver: newer is detected",
  update.compareVersions("152.0.0.1", "151.9.9.9") > 0);
check("semver: release beats prerelease of the same triple",
  update.compareVersions("151.0.0.0", "151.0.0.0-beta") > 0);
check("semver: garbage falls back to inequality",
  update.compareVersions("nightly-a", "nightly-b") !== 0);

const parsed = update.parseLatestRelease({
  tag_name: "v151.0.9000.1",
  assets: [
    { name: "Prism-mac-x64.dmg", browser_download_url: "http://x/x64.dmg" },
    { name: "Prism-mac-arm64.dmg", browser_download_url: "http://x/arm64.dmg" },
  ],
});
check("manifest: parses tag and picks the arch asset",
  parsed?.latestVersion === "151.0.9000.1" &&
    parsed?.downloadUrl === "http://x/arm64.dmg", JSON.stringify(parsed));
check("manifest: malformed JSON shape yields null",
  update.parseLatestRelease({ nope: 1 }) === null);
check("manifest: missing arch asset keeps version, nulls the download",
  update.parseLatestRelease({ tag_name: "v1.2.3", assets: [] })?.downloadUrl === null);

// ---- loopback mock server ----
const hits = { manifest: 0, asset: 0, slow: 0, missing: 0 };
const LATEST = "151.0.9999.0";
let dmgBytes = null;
const server = http.createServer((req, res) => {
  if (req.url === "/releases/latest") {
    hits.manifest++;
    res.setHeader("content-type", "application/json");
    res.end(JSON.stringify({
      tag_name: `v${LATEST}`,
      assets: [{
        name: "Prism-mac-arm64.dmg",
        browser_download_url: `${base}/Prism-mac-arm64.dmg`,
      }],
    }));
    return;
  }
  if (req.url === "/Prism-mac-arm64.dmg") {
    hits.asset++;
    res.end(dmgBytes);
    return;
  }
  if (req.url === "/slow") {
    hits.slow++;
    return;  // never answers
  }
  hits.missing++;
  res.statusCode = 404;
  res.end("nope");
});
await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
const port = server.address().port;
const base = `http://127.0.0.1:${port}`;

const tmpdir = fs.mkdtempSync(path.join(os.tmpdir(), "prism-update-test-"));
const env = { PRISM_UPDATE_URL: `${base}/releases/latest` };

try {
  // ---- patient check against the mock ----
  const current = "Prism/151.0.7922.174";
  let status = await update.checkForUpdate({ currentVersion: current, env, tmpdir });
  check("check: newer release detected with asset URL",
    status?.updateAvailable === true && status?.latestVersion === LATEST &&
      status?.downloadUrl === `${base}/Prism-mac-arm64.dmg`,
    JSON.stringify(status));

  status = await update.checkForUpdate({ currentVersion: `Prism/${LATEST}`, env, tmpdir });
  check("check: same version means no update",
    status?.updateAvailable === false, JSON.stringify(status));

  const missEnv = { PRISM_UPDATE_URL: `${base}/missing` };
  status = await update.checkForUpdate({ currentVersion: current, env: missEnv, tmpdir });
  check("check: 404 is a silent no-update", status === null);

  const slowEnv = { PRISM_UPDATE_URL: `${base}/slow` };
  const slowStart = Date.now();
  status = await update.checkForUpdate({ currentVersion: current, env: slowEnv, tmpdir });
  const slowMs = Date.now() - slowStart;
  check("check: hung endpoint gives up inside the timeout",
    status === null && slowMs < update.UPDATE_FETCH_TIMEOUT_MS + 3000,
    `${slowMs}ms`);

  // ---- write-behind cache ----
  const freshTmp = fs.mkdtempSync(path.join(os.tmpdir(), "prism-update-cache-"));
  const before = hits.manifest;
  update.startUpdateCheck({ currentVersion: current, env, tmpdir: freshTmp });
  await new Promise((r) => setTimeout(r, 1200));  // let the background fetch land
  check("cache: background refresh hit the server once",
    hits.manifest === before + 1, `hits=${hits.manifest - before}`);
  const cached = update.readCachedUpdate({ currentVersion: current, env, tmpdir: freshTmp });
  check("cache: fresh entry reads back with the update flag",
    cached?.updateAvailable === true && cached?.latestVersion === LATEST,
    JSON.stringify(cached));
  update.startUpdateCheck({ currentVersion: current, env, tmpdir: freshTmp });
  update.startUpdateCheck({ currentVersion: current, env, tmpdir: freshTmp });
  await new Promise((r) => setTimeout(r, 400));
  check("cache: fresh cache means no refetch (write-behind)",
    hits.manifest === before + 1);
  const stale = update.readCachedUpdate({
    currentVersion: "Prism/999.0.0.0", env, tmpdir: freshTmp,
  });
  check("cache: entry from another version is not reused", stale === null);

  // ---- upgrade --dry-run via the real CLI (kernel transport) ----
  const { spawnBrowser } = await import(path.join(hostSrc, "chrome.js"));
  const sockPath = path.join(tmpdir, "agent.sock");
  const browserPath = process.env.PRISM_BROWSER_PATH || path.join(
    os.homedir(), "chromium/prism/src/out/Prism-arm64/Prism.app/Contents/MacOS/Prism");
  const browser = spawnBrowser({
    browserPath,
    profileDir: path.join(tmpdir, "profile"),
    extraArgs: [`--prism-agent-socket=${sockPath}`],
    usePipe: false,
  });
  try {
    // wait for the socket
    const deadline = Date.now() + 30_000;
    while (Date.now() < deadline && !fs.existsSync(sockPath)) {
      await new Promise((r) => setTimeout(r, 200));
    }
    check("cli: fork agent socket up", fs.existsSync(sockPath));

    const cli = path.join(hostSrc, "cli.js");
    const cliEnv = {
      ...process.env,
      PRISM_UPDATE_URL: env.PRISM_UPDATE_URL,
      PRISM_AGENT_SOCKET: sockPath,
    };

    // spawn + explicit stdin end: execFileAsync's `input` option left the
    // CLI waiting on stdin forever here (observed), this form does not.
    const runCli = async (args, stdinText) => {
      const { spawn } = await import("node:child_process");
      const child = spawn("node", [cli, ...args], {
        env: cliEnv, stdio: ["pipe", "pipe", "pipe"],
      });
      let stdout = "", stderr = "";
      child.stdout.on("data", (d) => (stdout += d));
      child.stderr.on("data", (d) => (stderr += d));
      if (stdinText !== undefined) {
        child.stdin.write(stdinText);
        child.stdin.end();
      } else {
        child.stdin.end();
      }
      const code = await new Promise((resolve) => {
        const timer = setTimeout(() => {
          child.kill("SIGTERM");
          resolve(-1);
        }, 25_000);
        child.on("close", (c) => {
          clearTimeout(timer);
          resolve(c);
        });
      });
      return { code, stdout, stderr };
    };

    // The CLI reads the update cache from the default tmpdir — prime it there
    // (endpoint-keyed, so no cross-run collision) and clean up afterwards.
    await update.checkForUpdate({
      currentVersion: "Prism/151.0.7922.174",
      env: { PRISM_UPDATE_URL: env.PRISM_UPDATE_URL },
    });

    const bannerRun = await runCli([], `console.log("banner-probe");\n`);
    const bannerText = `${bannerRun.stdout}\n${bannerRun.stderr}`;
    check("banner: CLI prints the notice line with the latest version",
      bannerText.includes("[prism-browser:notice]") && bannerText.includes(LATEST),
      JSON.stringify(bannerText.slice(0, 300)));

    const assetHitsBefore = hits.asset;
    const dryRunResult = await runCli(["upgrade", "--dry-run"]);
    const dryRun = `${dryRunResult.stdout}\n${dryRunResult.stderr}`;
    const dryRunCode = dryRunResult.code;
    check("upgrade --dry-run prints the plan and downloads nothing",
      dryRunCode === 0 &&
        /dry-run: would upgrade Prism/.test(dryRun) &&
        dryRun.includes("download http") &&
        dryRun.includes("/Applications/Prism.app") &&
        dryRun.includes(LATEST) &&
        hits.asset === assetHitsBefore,
      dryRun.slice(0, 400));
  } finally {
    browser.kill("SIGTERM");
  }

  // ---- upgrade when already latest ----
  const upToDate = await upgrade.runUpgrade({
    currentVersion: `Prism/${LATEST}`,
    log: () => {},
    appPath: path.join(tmpdir, "fake-apps", "Prism.app"),
    deps: {},
  });
  // (no fake app exists yet — must report that, not crash)
  check("upgrade: missing install is reported, exit 1", upToDate === 1);

  const fakeApps = path.join(tmpdir, "fake-apps");
  const fakeApp = path.join(fakeApps, "Prism.app");
  fs.mkdirSync(path.join(fakeApp, "Contents"), { recursive: true });
  fs.writeFileSync(path.join(fakeApp, "Contents", "Info.plist"), "placeholder");
  const upToDateLog = [];
  const code = await upgrade.runUpgrade({
    currentVersion: `Prism/${LATEST}`,
    log: (l) => upToDateLog.push(l),
    appPath: fakeApp,
    env,
  });
  check("upgrade: already latest exits 0 with an explicit message",
    code === 0 && upToDateLog.some((l) => l.includes("no update available")),
    upToDateLog.join(" | "));

  // ---- real mechanics: fixture dmg + temp app dir swap ----
  // Build a real dmg carrying a Prism.app whose Info.plist reports LATEST.
  const dmgSrc = fs.mkdtempSync(path.join(os.tmpdir(), "prism-dmg-src-"));
  const stagedApp = path.join(dmgSrc, "Prism.app", "Contents");
  fs.mkdirSync(stagedApp, { recursive: true });
  fs.writeFileSync(path.join(stagedApp, "Info.plist"),
    `<?xml version="1.0" encoding="UTF-8"?>\n<plist version="1.0"><dict><key>CFBundleShortVersionString</key><string>${LATEST}</string></dict></plist>\n`);
  const fixtureDmg = path.join(tmpdir, "fixture.dmg");
  execFileSync("hdiutil", ["create", "-volname", "Prism", "-srcfolder", dmgSrc,
    "-format", "UDRO", "-ov", fixtureDmg], { stdio: "pipe" });
  dmgBytes = fs.readFileSync(fixtureDmg);

  const calls = [];
  const code2 = await upgrade.runUpgrade({
    currentVersion: current,
    log: (l) => calls.push(l),
    env: { PRISM_UPDATE_URL: `${base}/releases/latest` },
    appPath: fakeApp,
    deps: {
      async quitRunningApp() { calls.push("quit"); },
      async reopenApp() { calls.push("reopen"); },
    },
  });
  const swapped = fs.readFileSync(path.join(fakeApp, "Contents", "Info.plist"), "utf8");
  check("upgrade: full real path swaps the app and reopens it",
    code2 === 0 &&
      swapped.includes(LATEST) &&
      calls.includes("quit") && calls.includes("reopen") &&
      hits.asset > 0,
    `code=${code2} calls=${calls.join(",")}`);
  check("upgrade: no backup left behind",
    fs.readdirSync(fakeApps).filter((f) => f.includes("prism-backup")).length === 0);
} finally {
  server.close();
  fs.rmSync(tmpdir, { recursive: true, force: true });
  // Drop the endpoint-keyed cache this run primed into the default tmpdir.
  try {
    fs.rmSync(update.updateCachePath({ PRISM_UPDATE_URL: `${base}/releases/latest` }));
  } catch {}
}

console.log(failures ? `UPDATE FLOW TEST FAILED (${failures})` : "UPDATE FLOW TEST OK");
process.exit(failures ? 1 : 0);
