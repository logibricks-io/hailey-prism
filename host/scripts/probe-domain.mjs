// Direct kernel-domain probe: spawns the fork and calls Prism.* methods with
// no daemon in the middle, proving the custom DevTools domain is alive — over
// BOTH transports:
//   1. --remote-debugging-pipe (fd 3/4, single client), and
//   2. the kernel agent socket (--prism-agent-socket=<tmp>, one DevTools
//      session per connection), including a two-client parallel isolation
//      check: each connection owns its spaces/tabs and cannot see the other
//      client's.
//
// Run: node host/scripts/probe-domain.mjs   (from the repo root)
// Env: PRISM_BROWSER_PATH (defaults to the fork build output)

import net from "node:net";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const hostSrc = path.join(path.dirname(fileURLToPath(import.meta.url)), "..", "src");
const { spawnBrowser } = await import(path.join(hostSrc, "chrome.js"));
const { PipeTransport } = await import(path.join(hostSrc, "pipe.js"));
const { CdpConnection } = await import(path.join(hostSrc, "cdp.js"));

const browserPath =
  process.env.PRISM_BROWSER_PATH ||
  path.join(
    os.homedir(),
    "chromium/prism/src/out/Prism-arm64/Prism.app/Contents/MacOS/Prism",
  );

let failures = 0;
const check = (label, cond, detail) => {
  console.log(`${cond ? "PASS" : "FAIL"}  ${label}${detail ? ` — ${detail}` : ""}`);
  if (!cond) failures++;
};

// The shared kernel-domain assertion suite, transport-agnostic.
async function runSuite(cdp, label) {
  const version = await cdp.send("Browser.getVersion");
  check(`${label}: Browser.getVersion`, !!version.product, version.product);

  const empty = await cdp.send("Prism.listTaskSpaces");
  check(`${label}: Prism.listTaskSpaces (empty)`, Array.isArray(empty.taskSpaces), JSON.stringify(empty));

  const created = await cdp.send("Prism.createTaskSpace", { name: "kernel-probe" });
  const space = created.taskSpace;
  check(`${label}: Prism.createTaskSpace`, space && space.id > 0 && space.ownership === "agent",
    JSON.stringify(space));

  const used = await cdp.send("Prism.useTaskSpace", { id: space.id });
  check(`${label}: Prism.useTaskSpace`, used.taskSpace?.id === space.id, JSON.stringify(used.taskSpace));

  const ver = await cdp.send("Prism.getBrowserVersion");
  check(`${label}: Prism.getBrowserVersion`, !!ver.currentVersion, ver.currentVersion);

  // Real WebContents creation: a hidden agent tab in the selected space.
  const tab = await cdp.send("Prism.createTab", { url: "https://example.com" });
  check(`${label}: Prism.createTab`, typeof tab.targetId === "string" && tab.targetId.length > 0,
    tab.targetId);

  // Navigation is async; createTab returns before the load commits. Poll the
  // bookkeeping until the commit lands (example.com has proven flaky for a
  // fixed short wait).
  let listed;
  const commitDeadline = Date.now() + 15_000;
  while (Date.now() < commitDeadline) {
    const tabs = await cdp.send("Prism.listTabs");
    listed = tabs.tabs?.find((t) => t.targetId === tab.targetId);
    if (listed?.url?.startsWith("https://example.com") && listed.title) break;
    await new Promise((resolve) => setTimeout(resolve, 250));
  }
  check(`${label}: Prism.listTabs (created tab present)`,
    !!listed && listed.url.startsWith("https://example.com") && listed.active === true,
    JSON.stringify(listed));

  // Standard CDP interop: the targetId must be attachable as a page target.
  const attached = await cdp.send("Target.attachToTarget", { targetId: tab.targetId, flatten: true });
  check(`${label}: Target.attachToTarget`, !!attached.sessionId, attached.sessionId);

  const evaluated = await cdp.send("Runtime.evaluate",
    { expression: "document.title", returnByValue: true }, attached.sessionId);
  check(`${label}: Runtime.evaluate document.title`,
    evaluated.result?.value?.includes("Example Domain"),
    JSON.stringify(evaluated.result?.value));

  try {
    await cdp.send("Target.detachFromTarget", { sessionId: attached.sessionId });
  } catch { /* detach is best-effort in the probe */ }

  try {
    await cdp.send("Prism.closeTaskSpace");
    check(`${label}: Prism.closeTaskSpace`, true);
  } catch (error) {
    check(`${label}: Prism.closeTaskSpace`, false, error.message);
  }

  // Post-close guard: the session no longer has a selected space.
  try {
    await cdp.send("Prism.listTabs");
    check(`${label}: Prism.listTabs (unselected after close)`, false, "expected rejection");
  } catch (error) {
    check(`${label}: Prism.listTabs (unselected after close)`,
      error.message.includes("PRISM_TASK_SPACE_NOT_SELECTED"), error.message);
  }
}

async function connectSocketCdp(sockPath) {
  const socket = net.createConnection(sockPath);
  await new Promise((resolve, reject) => {
    socket.once("connect", resolve);
    socket.once("error", reject);
  });
  return new CdpConnection(new PipeTransport({ writeStream: socket, readStream: socket }));
}

// Two concurrent socket clients must not see each other's spaces or tabs:
// the kernel's selected-space state is per DevTools session.
async function runParallelIsolation(sockPath) {
  const a = await connectSocketCdp(sockPath);
  const b = await connectSocketCdp(sockPath);
  try {
    const spaceA = (await a.send("Prism.createTaskSpace", { name: "client-a" })).taskSpace;
    const spaceB = (await b.send("Prism.createTaskSpace", { name: "client-b" })).taskSpace;
    await a.send("Prism.useTaskSpace", { id: spaceA.id });
    await b.send("Prism.useTaskSpace", { id: spaceB.id });

    const tabA = (await a.send("Prism.createTab", { url: "https://example.com?a" })).targetId;
    const tabB = (await b.send("Prism.createTab", { url: "https://example.com?b" })).targetId;

    const listA = (await a.send("Prism.listTabs")).tabs.map((t) => t.targetId);
    const listB = (await b.send("Prism.listTabs")).tabs.map((t) => t.targetId);
    check("socket: client A sees only its own tab",
      listA.includes(tabA) && !listA.includes(tabB), JSON.stringify(listA));
    check("socket: client B sees only its own tab",
      listB.includes(tabB) && !listB.includes(tabA), JSON.stringify(listB));

    // Space registries are per session too: B never saw A's space.
    const spacesB = (await b.send("Prism.listTaskSpaces")).taskSpaces.map((s) => s.name);
    check("socket: client B cannot list client A's space",
      spacesB.includes("client-b") && !spacesB.includes("client-a"), JSON.stringify(spacesB));

    // Using the other client's space must fail. Space ids are per-session
    // namespaces (both start at 1), so give A a second space: id 2 exists only
    // in A's session, and B addressing it must be NOT_FOUND.
    const spaceA2 = (await a.send("Prism.createTaskSpace", { name: "client-a-2" })).taskSpace;
    let crossUse = null;
    try {
      await b.send("Prism.useTaskSpace", { id: spaceA2.id });
    } catch (error) {
      crossUse = error.message;
    }
    check("socket: cross-client useTaskSpace rejected",
      !!crossUse?.includes("PRISM_TASK_SPACE_NOT_FOUND"), crossUse ?? "(unexpectedly allowed)");

    await a.send("Prism.closeTaskSpace");
    await b.send("Prism.closeTaskSpace");
  } finally {
    a.close?.();
    b.close?.();
  }
}

// Phase 3 kernel snapshot suite: a nested-iframe fixture over loopback. The
// fixture serves one HTTP server on 127.0.0.1; pages reference iframes on
// "localhost" (same port, different host name) — different site, so the child
// frame is out-of-process (OOPIF). Three nesting levels:
//   L0 http://127.0.0.1:PORT/top  → L1 http://localhost:PORT/mid (OOPIF)
//                                 → L2 http://127.0.0.1:PORT/leaf (in L0's process)
async function runSnapshotSuite(cdp, fixtureOriginA, fixtureOriginB) {
  const created = await cdp.send("Prism.createTaskSpace", { name: "snapshot-probe" });
  await cdp.send("Prism.useTaskSpace", { id: created.taskSpace.id });

  const tab = await cdp.send("Prism.createTab", { url: `${fixtureOriginA}/top` });
  // Let all nested frames settle.
  await new Promise((resolve) => setTimeout(resolve, 3000));

  const snap = await cdp.send("Prism.snapshot", {});
  const snapVerbose = process.env.PRISM_PROBE_VERBOSE
    ? `\n${snap.content}`
    : `${snap.content.length}B, ${snap.refs.length} refs`;
  console.log(`  snapshot content:${snapVerbose}`);
  check("snapshot: returns content + refs",
    typeof snap.content === "string" && Array.isArray(snap.refs),
    `content=${snap.content?.length ?? "?"}B refs=${snap.refs?.length ?? "?"}`);
  check("snapshot: contains the OOPIF mid-frame button with a ref",
    /- button "mid-level action" \[ref=\d+/.test(snap.content),
    snap.content.split("\n").filter((l) => l.includes("mid-level")).join(" / "));
  check("snapshot: contains the third-level (leaf) input with a ref",
    /- (textbox|searchbox) "leaf-level field" \[ref=\d+/.test(snap.content),
    snap.content.split("\n").filter((l) => l.includes("leaf-level")).join(" / "));
  check("snapshot: link url annotation present",
    /\[ref=\d+, loc=role:link\[name="top docs"\], url=https?:\/\//.test(snap.content) ||
      /url=https?:\/\/.*top-docs/.test(snap.content),
    snap.content.split("\n").filter((l) => l.includes("link")).slice(0, 2).join(" / "));
  const lineOf = (needle) => snap.content.split("\n").find((l) => l.includes(needle)) ?? "";
  const indentOf = (needle) => lineOf(needle).match(/^ */)[0].length;
  check("snapshot: iframe content is nested below its owner",
    snap.content.indexOf("top-level action") < snap.content.indexOf("mid-level action") &&
      snap.content.indexOf("mid-level action") < snap.content.indexOf("leaf-level field") &&
      indentOf("mid-level action") > indentOf("top-level action") &&
      indentOf("leaf-level field") > indentOf("mid-level action"),
    `indents top=${indentOf("top-level action")} mid=${indentOf("mid-level action")} leaf=${indentOf("leaf-level field")}`);

  // ref stability: same element, same backendNodeId across calls.
  const refOf = (s, name) => s.refs.find((r) => r.name === name)?.backendNodeId;
  const snapAgain = await cdp.send("Prism.snapshot", {});
  check("snapshot: ref stable across calls",
    refOf(snap, "top-level action") != null &&
      refOf(snap, "top-level action") === refOf(snapAgain, "top-level action"),
    `first=${refOf(snap, "top-level action")} second=${refOf(snapAgain, "top-level action")}`);

  // maxResultLength truncation.
  const truncated = await cdp.send("Prism.snapshot", { maxResultLength: 120 });
  check("snapshot: maxResultLength truncates content",
    truncated.content.length === 120, `len=${truncated.content.length}`);

  // only_within_viewport drops the off-screen button.
  const scoped = await cdp.send("Prism.snapshot", { scope: "only_within_viewport" });
  check("snapshot: only_within_viewport drops off-viewport actionable",
    !scoped.content.includes("below-the-fold action") &&
      scoped.content.includes("top-level action"),
    scoped.content.split("\n").filter((l) => l.includes("action")).join(" / "));

  // An empty (no-tab) space resolves an empty snapshot, not an error.
  const fresh = await cdp.send("Prism.createTaskSpace", { name: "snapshot-empty" });
  await cdp.send("Prism.useTaskSpace", { id: fresh.taskSpace.id });
  const emptySnap = await cdp.send("Prism.snapshot", {});
  check("snapshot: no-tab space resolves empty",
    emptySnap.content === "" && emptySnap.refs.length === 0,
    JSON.stringify(emptySnap));

  await cdp.send("Prism.closeTaskSpace");  // empty space
  await cdp.send("Prism.useTaskSpace", { id: created.taskSpace.id });
  await cdp.send("Prism.closeTaskSpace");  // the fixture space (tabs die with it)
}

// Starts the nested-iframe fixture server; returns { port, close }.
async function startIframeFixture() {
  const { createServer } = await import("node:http");
  const page = (title, body) =>
    `<!doctype html><html><head><title>${title}</title></head><body>${body}</body></html>`;
  const server = createServer((req, res) => {
    const host = req.headers.host?.split(":")[0];
    const port = new URL(`http://${req.headers.host}`).port;
    const a = `127.0.0.1:${port}`;   // site A (this server when addressed as 127.0.0.1)
    const b = `localhost:${port}`;   // site B (same server, different site)
    if (req.url === "/top") {
      res.end(page("fixture top", `
        <h1>top level</h1>
        <button>top-level action</button>
        <a href="http://${a}/top-docs">top docs</a>
        <button style="position:absolute;top:5000px">below-the-fold action</button>
        <iframe src="http://${b}/mid" style="width:600px;height:400px"></iframe>`));
    } else if (req.url === "/mid") {
      res.end(page("fixture mid", `
        <p>mid level</p>
        <button>mid-level action</button>
        <iframe src="http://${a}/leaf" style="width:400px;height:200px"></iframe>`));
    } else if (req.url === "/leaf") {
      res.end(page("fixture leaf", `
        <p>leaf level</p>
        <input aria-label="leaf-level field" type="text">`));
    } else {
      res.end(page("fixture", "<p>ok</p>"));
    }
  });
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  const port = server.address().port;
  return {
    originA: `http://127.0.0.1:${port}`,
    originB: `http://localhost:${port}`,
    close: () => server.close(),
  };
}

async function runPipeSuite(profileDir, fixture) {
  const child = spawnBrowser({ browserPath, profileDir });
  const transport = new PipeTransport({
    writeStream: child.stdio[3],
    readStream: child.stdio[4],
  });
  const cdp = new CdpConnection(transport);
  try {
    await runSuite(cdp, "pipe");
    await runSnapshotSuite(cdp, fixture.originA, fixture.originB);
  } catch (error) {
    check("pipe: unexpected transport failure", false, error.message);
  } finally {
    child.kill("SIGTERM");
  }
}

async function runSocketSuites(profileDir) {
  const sockPath = path.join(profileDir, "agent.sock");
  const child = spawnBrowser({
    browserPath,
    profileDir,
    extraArgs: [`--prism-agent-socket=${sockPath}`],
    usePipe: false,
  });
  try {
    // Wait for the listener to come up.
    let cdp = null;
    const deadline = Date.now() + 30_000;
    while (Date.now() < deadline && !cdp) {
      await new Promise((resolve) => setTimeout(resolve, 150));
      cdp = await connectSocketCdp(sockPath).catch(() => null);
    }
    if (!cdp) {
      check("socket: connect to agent socket", false, sockPath);
      return;
    }
    check("socket: connect to agent socket", true, sockPath);
    try {
      await runSuite(cdp, "socket");
    } catch (error) {
      check("socket: unexpected transport failure", false, error.message);
    }
    cdp.close?.();
    await runParallelIsolation(sockPath);
  } finally {
    child.kill("SIGTERM");
  }
}

const stamp = Date.now();
const fixture = await startIframeFixture();
try {
  await runPipeSuite(path.join(os.tmpdir(), `prism-probe-pipe-${stamp}`), fixture);
  await runSocketSuites(path.join(os.tmpdir(), `prism-probe-sock-${stamp}`));
} finally {
  fixture.close();
}

console.log(failures ? `PROBE FAILED (${failures})` : "PROBE OK");
process.exit(failures ? 1 : 0);
