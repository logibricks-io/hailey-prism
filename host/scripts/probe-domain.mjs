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

  // Real WebContents creation: a hidden agent tab in the selected space.
  const tab = await cdp.send("Prism.createTab", { url: "https://example.com" });
  check("Prism.createTab", typeof tab.targetId === "string" && tab.targetId.length > 0,
    tab.targetId);

  // Navigation is async; createTab returns before the load commits.
  await new Promise((resolve) => setTimeout(resolve, 2000));

  const tabs = await cdp.send("Prism.listTabs");
  const listed = tabs.tabs?.find((t) => t.targetId === tab.targetId);
  check("Prism.listTabs (created tab present)",
    !!listed && listed.url.startsWith("https://example.com") && listed.active === true,
    JSON.stringify(tabs.tabs));

  // Standard CDP interop: the targetId must be attachable as a page target.
  const attached = await cdp.send("Target.attachToTarget", { targetId: tab.targetId, flatten: true });
  check("Target.attachToTarget", !!attached.sessionId, attached.sessionId);

  const evaluated = await cdp.send("Runtime.evaluate",
    { expression: "document.title", returnByValue: true }, attached.sessionId);
  check("Runtime.evaluate document.title",
    evaluated.result?.value?.includes("Example Domain"),
    JSON.stringify(evaluated.result?.value));

  try {
    await cdp.send("Target.detachFromTarget", { sessionId: attached.sessionId });
  } catch { /* detach is best-effort in the probe */ }

  try {
    await cdp.send("Prism.closeTaskSpace");
    check("Prism.closeTaskSpace", true);
  } catch (error) {
    check("Prism.closeTaskSpace", false, error.message);
  }

  // Post-close guard: the session no longer has a selected space.
  try {
    await cdp.send("Prism.listTabs");
    check("Prism.listTabs (unselected after close)", false, "expected rejection");
  } catch (error) {
    check("Prism.listTabs (unselected after close)",
      error.message.includes("PRISM_TASK_SPACE_NOT_SELECTED"), error.message);
  }
} catch (error) {
  check("unexpected transport failure", false, error.message);
} finally {
  child.kill("SIGTERM");
}

console.log(failures ? `PROBE FAILED (${failures})` : "PROBE OK");
process.exit(failures ? 1 : 0);
