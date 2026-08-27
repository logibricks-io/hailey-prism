// prism-host daemon: owns one browser process and multiplexes many CLI
// clients onto its single DevTools pipe.
//
// Wire protocol: newline-delimited JSON over a unix socket.
//   client -> daemon:
//     {kind:"call", id, method, params}   binding method invocation
//     {kind:"cdp",  payload}              raw CDP JSON string (fire-and-forget)
//   daemon -> client:
//     {id, result} | {id, error:{message, code}}     responses to "call"
//     {event:"cdp", message}                          raw CDP response/event string
//     {event:"cdpSendError", message, code}           local send failure broadcast
//     {event:"browserExit", reason}                   browser process died
//
// Per-connection state (selected space) lives here, which is what makes
// parallel agent runs on one browser possible in simulation mode.

import fs from "node:fs";
import net from "node:net";
import os from "node:os";
import path from "node:path";

import { CdpConnection } from "./cdp.js";
import { resolveBrowserPath, defaultProfileDir, spawnBrowser } from "./chrome.js";
import { PipeTransport } from "./pipe.js";
import { SpaceRegistry } from "./spaces.js";
import { snapshotTarget } from "./snapshot.js";

export const SOCKET_PATH =
  process.env.PRISM_HOST_SOCKET ||
  path.join(os.tmpdir(), `prism-host-${process.getuid?.() ?? 0}.sock`);

const CODES = {
  BROWSER_UNAVAILABLE: "PRISM_BROWSER_UNAVAILABLE",
  NOT_FOUND: "PRISM_TASK_SPACE_NOT_FOUND",
  NOT_SELECTED: "PRISM_TASK_SPACE_NOT_SELECTED",
  USER_IN_CONTROL: "PRISM_TASK_SPACE_USER_IN_CONTROL",
  INACTIVE: "PRISM_TASK_SPACE_INACTIVE",
  WEB_CONTENTS: "PRISM_WEB_CONTENTS_UNAVAILABLE",
  INVALID_ARGUMENT: "PRISM_INVALID_ARGUMENT",
  SNAPSHOT_FAILED: "PRISM_SNAPSHOT_FAILED",
};

class HostServer {
  #browser = null;
  #cdp = null;
  #registry = new SpaceRegistry();
  #clients = new Map(); // socket -> { id, selectedSpaceId }
  #sessions = new Map(); // sessionId -> client socket
  #targets = new Map(); // targetId -> targetInfo
  #nextClientId = 1;

  async start() {
    await this.#launchBrowser();
    const server = net.createServer((socket) => this.#onConnection(socket));
    try {
      fs.unlinkSync(SOCKET_PATH);
    } catch {}
    await new Promise((resolve, reject) => {
      server.once("error", reject);
      server.listen(SOCKET_PATH, resolve);
    });
    process.on("SIGTERM", () => this.#shutdown(server));
    process.on("SIGINT", () => this.#shutdown(server));
    return server;
  }

  async #launchBrowser() {
    const browserPath = resolveBrowserPath();
    if (!browserPath) {
      console.error("[prism-host] no browser found (set PRISM_BROWSER_PATH)");
      process.exit(1);
    }
    this.#browser = spawnBrowser({
      browserPath,
      profileDir: defaultProfileDir(),
    });
    if (process.env.PRISM_HOST_DEBUG) {
      console.error(`[prism-host] launched browser pid=${this.#browser.pid} path=${browserPath}`);
    }
    this.#browser.on("exit", (code) => {
      if (process.env.PRISM_HOST_DEBUG) {
        console.error(`[prism-host] browser exited code=${code}`);
      }
      for (const socket of this.#clients.keys()) {
        send(socket, { event: "browserExit", reason: `exit code ${code}` });
      }
    });
    const transport = new PipeTransport({
      writeStream: this.#browser.stdio[3],
      readStream: this.#browser.stdio[4],
    });
    this.#cdp = new CdpConnection(transport);
    this.#cdp.onEvent((message) => this.#onCdpEvent(message));
    // Keep a live target cache so per-space filtering never needs a round trip.
    await this.#cdp.send("Target.setDiscoverTargets", { discover: true });
  }

  #onConnection(socket) {
    const client = { id: this.#nextClientId++, selectedSpaceId: null };
    this.#clients.set(socket, client);
    let buffer = "";
    socket.setEncoding("utf8");
    socket.on("data", (chunk) => {
      buffer += chunk;
      let boundary;
      while ((boundary = buffer.indexOf("\n")) !== -1) {
        const line = buffer.slice(0, boundary);
        buffer = buffer.slice(boundary + 1);
        if (line.trim()) this.#onClientMessage(socket, client, line);
      }
    });
    const drop = () => {
      this.#clients.delete(socket);
      for (const [sessionId, owner] of this.#sessions) {
        if (owner === socket) this.#sessions.delete(sessionId);
      }
    };
    socket.on("close", drop);
    socket.on("error", drop);
  }

  async #onClientMessage(socket, client, line) {
    let message;
    try {
      message = JSON.parse(line);
    } catch {
      return;
    }
    if (message.kind === "cdp") {
      await this.#onRawCdp(socket, client, message.payload);
      return;
    }
    if (message.kind === "call") {
      await this.#onBindingCall(socket, client, message);
    }
  }

  // ---------------------------------------------------------------- raw CDP

  async #onRawCdp(socket, client, payload) {
    let request;
    try {
      request = JSON.parse(payload);
    } catch {
      return this.#sendError(socket, "malformed CDP payload", CODES.INVALID_ARGUMENT);
    }
    const guard = this.#guardAgentControl(client);
    if (guard) {
      return this.#sendError(socket, guard.message, guard.code);
    }
    const { id: localId, method, sessionId } = request;
    let { params = {} } = request;

    try {
      // Contract §6: raw permission grants stay unexposed to agent scripts
      // (same as upstream). Answer with a CDP-style error so callers can
      // classify the capability as unsupported instead of hanging.
      if (method === "Browser.grantPermissions" || method === "Browser.setPermission") {
        return send(socket, { event: "cdp", message: JSON.stringify({ id: localId, error: { message: `${method} is not supported for agent scripts` } }) });
      }
      if (method === "Target.getTargets") {
        const targetInfos = this.#targetsForClient(client);
        return send(socket, { event: "cdp", message: JSON.stringify({ id: localId, result: { targetInfos } }) });
      }
      if (method === "Target.createTarget") {
        const result = await this.#createTabInSpace(client, params.url || "about:blank");
        if (result.error) {
          return send(socket, { event: "cdp", message: JSON.stringify({ id: localId, error: { message: result.error } }) });
        }
        return send(socket, { event: "cdp", message: JSON.stringify({ id: localId, result: { targetId: result.targetId } }) });
      }
      if (method === "Target.attachToTarget" && params.targetId) {
        const denied = this.#checkTargetAccess(client, params.targetId);
        if (denied) {
          return send(socket, { event: "cdp", message: JSON.stringify({ id: localId, error: { message: denied } }) });
        }
        const result = await this.#cdp.send(method, params, sessionId);
        if (result.sessionId) this.#sessions.set(result.sessionId, socket);
        return send(socket, { event: "cdp", message: JSON.stringify({ id: localId, result }) });
      }
      // Downloads: stock Chrome applies Browser.setDownloadBehavior to the
      // default browser context only, so downloads inside a simulated Space
      // (an isolated context) never complete. Scope the behavior to the
      // caller's selected Space context instead.
      if (method === "Browser.setDownloadBehavior" && !params.browserContextId) {
        const space =
          client.selectedSpaceId != null && this.#registry.get(client.selectedSpaceId);
        if (space?.browserContextId) {
          params = { ...params, browserContextId: space.browserContextId };
        }
      }
      // Verbatim passthrough for everything else (session-scoped or not).
      if (process.env.PRISM_HOST_DEBUG && (method === "Target.closeTarget" || method === "Target.createTarget" || method === "Target.activateTarget")) {
        console.error(`[prism-host] raw ${method} ${JSON.stringify(params)}`);
      }
      const result = await this.#cdp.send(method, params, sessionId);
      send(socket, { event: "cdp", message: JSON.stringify({ id: localId, result }) });
    } catch (error) {
      send(socket, {
        event: "cdp",
        message: JSON.stringify({ id: localId, error: { message: error.message } }),
      });
    }
  }

  #onCdpEvent(message) {
    const { method, params = {}, sessionId } = message;
    if (method === "Target.targetCreated" || method === "Target.targetInfoChanged") {
      if (params.targetInfo) this.#targets.set(params.targetInfo.targetId, params.targetInfo);
    }
    if (method === "Target.targetDestroyed") {
      this.#targets.delete(params.targetId);
      this.#registry.clearTarget(params.targetId);
    }
    if (process.env.PRISM_HOST_DEBUG && method?.startsWith("Target.target")) {
      const info = params.targetInfo;
      console.error(
        `[prism-host] ${method} ${info ? `${info.targetId}:${info.type}:${info.browserContextId || "default"}` : params.targetId}`,
      );
    }
    if (method === "Target.detachedFromTarget" && params.sessionId) {
      const owner = this.#sessions.get(params.sessionId);
      this.#sessions.delete(params.sessionId);
      if (owner) {
        send(owner, { event: "cdp", message: JSON.stringify(message) });
        return;
      }
    }
    if (sessionId) {
      const owner = this.#sessions.get(sessionId);
      if (owner) send(owner, { event: "cdp", message: JSON.stringify(message) });
      return;
    }
    // Browser-level events are broadcast; harness instances filter by the
    // sessions/targets they know about. Stock Chrome reports downloads as
    // Browser.download* while the harness's download facade (inherited from
    // upstream) listens for the legacy Page.download* names — translate in
    // the broadcast path so page.waitForEvent("download") works unmodified.
    if (
      method === "Browser.downloadWillBegin" ||
      method === "Browser.downloadProgress"
    ) {
      message = { ...message, method: `Page.${method.slice("Browser.".length)}` };
    }
    for (const socket of this.#clients.keys()) {
      send(socket, { event: "cdp", message: JSON.stringify(message) });
    }
  }

  // --------------------------------------------------------- binding calls

  async #onBindingCall(socket, client, { id, method, params = {} }) {
    try {
      const result = await this.#dispatchBinding(client, method, params);
      send(socket, { id, result: result ?? null });
    } catch (error) {
      send(socket, {
        id,
        error: { message: error.message, code: error.code || CODES.INVALID_ARGUMENT },
      });
    }
  }

  async #dispatchBinding(client, method, params) {
    switch (method) {
      case "host.ping":
        return { ok: true, clients: this.#clients.size };
      case "host.reset":
        await this.#resetBrowser();
        return { ok: true };

      case "getBrowserVersion": {
        const info = await this.#cdp.send("Browser.getVersion");
        return {
          currentVersion: info.product || "unknown",
          updateAvailable: false,
        };
      }

      case "listTaskSpaces":
        return { taskSpaces: this.#registry.list() };
      case "createTaskSpace": {
        const space = this.#registry.create(params.name);
        const { browserContextId } = await this.#cdp.send(
          "Target.createBrowserContext",
          {},
        );
        this.#registry.setBrowserContext(space.id, browserContextId);
        return this.#registry.get(space.id);
      }
      case "useTaskSpace": {
        const space = this.#registry.get(params.id);
        if (!space) throw hostError(`task space not found: ${params.id}`, CODES.NOT_FOUND);
        if (space.ownership === "user") {
          throw hostError("task space is controlled by the user", CODES.USER_IN_CONTROL);
        }
        client.selectedSpaceId = space.id;
        return space;
      }
      case "claimTaskSpace": {
        const claimed = this.#registry.claim(params.id);
        if (!claimed) throw hostError(`task space not found: ${params.id}`, CODES.NOT_FOUND);
        client.selectedSpaceId = Number(params.id);
        return claimed;
      }
      case "completeTaskSpace":
        this.#requireSelectedSpace(client);
        return { done: true };
      case "closeTaskSpace": {
        const space = this.#requireSelectedSpace(client);
        if (space.browserContextId) {
          await this.#cdp
            .send("Target.disposeBrowserContext", {
              browserContextId: space.browserContextId,
            })
            .catch(() => {});
        }
        this.#registry.remove(space.id);
        client.selectedSpaceId = null;
        return { done: true };
      }
      case "handOffTaskSpace": {
        const space = this.#requireSelectedSpace(client);
        return this.#registry.handOff(space.id);
      }
      case "takeOverTaskSpace": {
        const space = this.#requireSelectedSpace(client);
        return this.#registry.takeOver(space.id);
      }
      case "showTaskSpace":
        // Spaces on this transport are simulated as isolated browser
        // contexts — there is no shell window to show. The kernel (fork)
        // transport implements the real thing.
        throw hostError(
          "showTaskSpace requires the kernel transport (simulated spaces have no windows)",
          CODES.INVALID_ARGUMENT,
        );

      case "listTabs": {
        const space = this.#requireSelectedSpace(client);
        const tabs = this.#targetsForClient(client).map((info) => ({
          targetId: info.targetId,
          title: info.title,
          url: info.url,
          active: info.targetId === this.#registry.get(space.id)?.currentTargetId,
        }));
        return { tabs };
      }
      case "createTab": {
        const result = await this.#createTabInSpace(client, params.url || "about:blank");
        if (result.error) throw hostError(result.error, result.code || CODES.NOT_SELECTED);
        return { targetId: result.targetId };
      }
      case "snapshot": {
        const space = this.#requireSelectedSpace(client);
        if (space.ownership === "agentDelegatedToUser") {
          throw hostError("task space is controlled by the user", CODES.USER_IN_CONTROL);
        }
        const targetId = this.#registry.get(space.id)?.currentTargetId;
        if (!targetId) {
          // A freshly created simulated space has no tab yet (upstream spaces
          // always hold a window with a tab). The harness's agent-control probe
          // (waitForAgentControl) calls snapshot on such spaces and only treats
          // USER_IN_CONTROL as "not ready" — any other rejection fails the probe.
          // Resolve an empty snapshot so the probe reads "agent owns an empty
          // space" instead of dying on PRISM_WEB_CONTENTS_UNAVAILABLE.
          return { content: "", refs: [] };
        }
        try {
          return await snapshotTarget(this.#cdp, targetId, params);
        } catch (error) {
          throw hostError(`snapshot failed: ${error.message}`, CODES.SNAPSHOT_FAILED);
        }
      }
      default:
        throw hostError(`unknown binding method: ${method}`, CODES.INVALID_ARGUMENT);
    }
  }

  // -------------------------------------------------------------- helpers

  // Contract §1.3: local send failures are broadcast without a request id; the
  // harness rejects every pending request with the given code.
  #sendError(socket, message, code) {
    send(socket, { event: "cdpSendError", message, code });
  }

  #requireSelectedSpace(client) {
    const space = client.selectedSpaceId != null && this.#registry.get(client.selectedSpaceId);
    if (!space) throw hostError("no task space selected", CODES.NOT_SELECTED);
    return space;
  }

  #guardAgentControl(client) {
    if (client.selectedSpaceId == null) {
      return { message: "no task space selected", code: CODES.NOT_SELECTED };
    }
    const space = this.#registry.get(client.selectedSpaceId);
    if (!space) return { message: "task space not found", code: CODES.NOT_FOUND };
    if (space.ownership === "agentDelegatedToUser") {
      return { message: "task space is controlled by the user", code: CODES.USER_IN_CONTROL };
    }
    return null;
  }

  #checkTargetAccess(client, targetId) {
    const info = this.#targets.get(targetId);
    if (!this.#registry.ownsTarget(client.selectedSpaceId, info)) {
      return `target not in selected task space: ${targetId}`;
    }
    return null;
  }

  #targetsForClient(client) {
    const space = client.selectedSpaceId != null && this.#registry.get(client.selectedSpaceId);
    if (!space) return [];
    return [...this.#targets.values()].filter(
      (info) => info.type === "page" && info.browserContextId === space.browserContextId,
    );
  }

  async #createTabInSpace(client, url) {
    const space = client.selectedSpaceId != null && this.#registry.get(client.selectedSpaceId);
    if (!space) return { error: "no task space selected", code: CODES.NOT_SELECTED };
    if (!space.browserContextId) {
      return { error: "task space has no browser context", code: CODES.INACTIVE };
    }
    // Only page targets prove a live window. Chrome also exposes browser_ui
    // targets per context; they outlive the last page tab by a beat during
    // window teardown, and counting them here makes newWindow:false target a
    // window that is already gone ("Failed to open new tab - no browser is
    // open").
    const hasWindow = [...this.#targets.values()].some(
      (info) =>
        info.type === "page" &&
        info.browserContextId === space.browserContextId,
    );
    if (process.env.PRISM_HOST_DEBUG) {
      console.error(
        `[prism-host] createTabInSpace space=${space.id} ctx=${space.browserContextId} hasWindow=${hasWindow} knownTargets=${[...this.#targets.values()].map((t) => `${t.targetId}:${t.type}:${t.browserContextId || "default"}`).join(",")}`,
      );
    }
    // The target cache is event-driven and lags real window teardown: closing
    // the context's last page tab resolves before Target.targetDestroyed
    // arrives, so a createTarget with newWindow:false can still hit the window
    // that is already gone. Retry once with a forced fresh window.
    const createTarget = (newWindow) =>
      this.#cdp.send("Target.createTarget", {
        url,
        browserContextId: space.browserContextId,
        // First tab of a context opens its window; later tabs join it.
        newWindow,
        background: false,
      });
    let created;
    try {
      created = await createTarget(!hasWindow);
    } catch (error) {
      if (!/no browser is open/i.test(error?.message || "")) throw error;
      if (process.env.PRISM_HOST_DEBUG) {
        console.error(
          `[prism-host] createTabInSpace space=${space.id} stale-window race, retrying with newWindow:true`,
        );
      }
      created = await createTarget(true);
    }
    this.#registry.setCurrentTarget(space.id, created.targetId);
    return { targetId: created.targetId };
  }

  async #resetBrowser() {
    this.#sessions.clear();
    this.#targets.clear();
    this.#registry = new SpaceRegistry();
    for (const client of this.#clients.values()) client.selectedSpaceId = null;
    if (this.#browser) this.#browser.kill("SIGTERM");
    await new Promise((resolve) => setTimeout(resolve, 300));
    await this.#launchBrowser();
  }

  #shutdown(server) {
    server.close();
    if (this.#browser) this.#browser.kill("SIGTERM");
    try {
      fs.unlinkSync(SOCKET_PATH);
    } catch {}
    process.exit(0);
  }
}

function hostError(message, code) {
  const error = new Error(message);
  error.code = code;
  return error;
}

function send(socket, message) {
  try {
    socket.write(JSON.stringify(message) + "\n");
  } catch {}
}

if (
  process.argv[1] &&
  import.meta.url === (await import("node:url")).pathToFileURL(process.argv[1]).href
) {
  const server = new HostServer();
  server.start().catch((error) => {
    console.error(`[prism-host] fatal: ${error.message}`);
    process.exit(1);
  });
}
