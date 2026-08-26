// Direct kernel-domain probe: spawns a browser over --remote-debugging-pipe
// and calls Prism.* methods with no daemon in the middle, proving the custom
// DevTools domain is alive in our Chromium build.
//
// Run: node host/scripts/probe-domain.mjs   (from the repo root)
// Env: PRISM_BROWSER_PATH (defaults to the fork build output)

import path from "node:path";
import { fileURLToPath } from "node:url";

const hostSrc = path.join(path.dirname(fileURLToPath(import.meta.url)), "..", "src");
const { spawnBrowser } = await import(path.join(hostSrc, "chrome.js"));
const { PipeTransport } = await import(path.join(hostSrc, "pipe.js"));
const { CdpConnection } = await import(path.join(hostSrc, "cdp.js"));

const os = await import("node:os");
const browserPath =
  process.env.PRISM_BROWSER_PATH ||
  path.join(
    os.homedir(),
    "chromium/prism/src/out/Prism-arm64/Chromium.app/Contents/MacOS/Chromium",
  );

const profileDir = path.join(os.tmpdir(), `prism-probe-${Date.now()}`);
const child = spawnBrowser({ browserPath, profileDir });
const transport = new PipeTransport({
  writeStream: child.stdio[3],
  readStream: child.stdio[4],
});
const cdp = new CdpConnection(transport);

let failures = 0;
const check = (label, cond, detail) => {
  console.log(`${cond ? "PASS" : "FAIL"}  ${label}${detail ? ` — ${detail}` : ""}`);
  if (!cond) failures++;
};

try {
  const version = await cdp.send("Browser.getVersion");
  check("Browser.getVersion", !!version.product, version.product);

  const empty = await cdp.send("Prism.listTaskSpaces");
  check("Prism.listTaskSpaces (empty)", Array.isArray(empty.taskSpaces), JSON.stringify(empty));

  const created = await cdp.send("Prism.createTaskSpace", { name: "kernel-probe" });
  const space = created.taskSpace;
  check("Prism.createTaskSpace", space && space.id > 0 && space.ownership === "agent",
    JSON.stringify(space));

  const used = await cdp.send("Prism.useTaskSpace", { id: space.id });
  check("Prism.useTaskSpace", used.taskSpace?.id === space.id, JSON.stringify(used.taskSpace));

  const ver = await cdp.send("Prism.getBrowserVersion");
  check("Prism.getBrowserVersion", !!ver.currentVersion, ver.currentVersion);

  // Unselected-session guard: a fresh connection probing snapshot must fail
  // with the contract's not-selected code.
  try {
    await cdp.send("Prism.closeTaskSpace");
    check("Prism.closeTaskSpace", true);
  } catch (error) {
    check("Prism.closeTaskSpace", false, error.message);
  }
} catch (error) {
  check("unexpected transport failure", false, error.message);
} finally {
  child.kill("SIGTERM");
}

console.log(failures ? `PROBE FAILED (${failures})` : "PROBE OK");
process.exit(failures ? 1 : 0);
