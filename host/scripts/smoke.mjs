// Smoke test for the prism-host stack without the harness bundle:
// daemon lifecycle, space simulation, tab creation, snapshot composition.
//
// Run: node host/scripts/smoke.mjs   (from the repo root)

import path from "node:path";
import { fileURLToPath } from "node:url";

const hostSrc = path.join(
  path.dirname(fileURLToPath(import.meta.url)),
  "..",
  "src",
);
const { connectToDaemon, buildPrismBindings } = await import(
  path.join(hostSrc, "client.js")
);
const { SOCKET_PATH } = await import(path.join(hostSrc, "daemon.js"));

// Boot the daemon the same way the CLI does.
const { spawn } = await import("node:child_process");
try {
  const probe = await connectToDaemon(SOCKET_PATH);
  await probe.call("host.ping");
  probe.close();
} catch {
  const child = spawn(
    process.execPath,
    [path.join(hostSrc, "daemon.js")],
    { detached: true, stdio: "ignore" },
  );
  child.unref();
  await new Promise((resolve) => setTimeout(resolve, 2500));
}

const client = await connectToDaemon(SOCKET_PATH);
const prism = buildPrismBindings(client);
const fail = (message) => {
  console.error("FAIL:", message);
  process.exitCode = 1;
};

const version = await prism.getBrowserVersion();
console.log("browser:", version.currentVersion);

const space = await prism.createTaskSpace("smoke");
if (space.error) fail(`createTaskSpace: ${space.error}`);
console.log("space:", space.id, space.name, space.ownership);

const used = await prism.useTaskSpace(space.id);
if (used?.error) fail(`useTaskSpace: ${used.error}`);

const tab = await prism.createTab("https://example.com");
if (tab.error) fail(`createTab: ${tab.error}`);
console.log("tab:", tab.targetId);

await new Promise((resolve) => setTimeout(resolve, 1500));

const { tabs } = await prism.listTabs();
console.log("tabs:", tabs.length, tabs[0]?.url);
if (tabs.length !== 1) fail(`expected 1 tab in space, got ${tabs.length}`);

// Poll until the page has actually loaded (window creation races navigation).
let snap = null;
for (let attempt = 0; attempt < 20; attempt++) {
  snap = await prism.snapshot();
  if (!snap?.error && snap.content.includes("Example Domain")) break;
  await new Promise((resolve) => setTimeout(resolve, 400));
}
if (snap?.error) fail(`snapshot: ${snap.error}`);
else {
  const lines = snap.content.split("\n");
  console.log(`snapshot: ${lines.length} lines, ${snap.refs.length} refs`);
  console.log(lines.slice(0, 6).join("\n"));
  if (!snap.content.includes("Example Domain")) fail("snapshot missing page text");
}

const done = await prism.closeTaskSpace();
if (done?.error) fail(`closeTaskSpace: ${done.error}`);

client.close();
console.log(process.exitCode ? "SMOKE FAILED" : "SMOKE OK");
process.exit(process.exitCode ?? 0);
