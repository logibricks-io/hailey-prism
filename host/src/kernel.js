// Kernel transport: a direct NUL-framed CDP connection to the fork's agent
// socket (the browser-side listener is //prism/browser/agent_socket).
//
// Each CLI process owns its socket connection end to end — unlike the daemon
// transport there is no multiplexer in between, so the kernel's per-session
// Prism.* selected-space state maps 1:1 onto this process. Wire format is
// identical to --remote-debugging-pipe: single JSON payloads terminated by a
// NUL byte, which lets us reuse the pipe framing verbatim.
//
// Two traffic classes share the connection:
//   - sendCDPMessage: the harness's raw payloads are forwarded with their ids
//     rewritten into our internal id space (collisions with harness-assigned
//     ids would otherwise misroute responses); responses get their original id
//     restored before they reach prism.onCDPMessage.
//   - Binding calls: Prism.* methods are awaited request/response pairs over
//     the same internal id space. Kernel failures carry the stable
//     "PRISM_X: detail" prefix (contract §3.2); it is split back into the
//     daemon's {message, code} shape here.

import net from "node:net";
import os from "node:os";
import path from "node:path";

import { snapshotTarget } from "./snapshot.js";
import { defaultProfileDir } from "./chrome.js";

// The kernel binds its listener per profile when launched with
// --user-data-dir (every dev/test spawn) and at the global default otherwise.
// Try both, profile first (that's what our own spawns produce).
const GLOBAL_SOCKET_PATH = path.join(
  os.homedir(), "Library", "Application Support", "Prism", "agent.sock",
);

export const KERNEL_SOCKET_PATH =
  process.env.PRISM_AGENT_SOCKET && process.env.PRISM_AGENT_SOCKET !== "off"
    ? process.env.PRISM_AGENT_SOCKET
    : null;

export function kernelSocketCandidates() {
  if (KERNEL_SOCKET_PATH) return [KERNEL_SOCKET_PATH];
  return [
    path.join(defaultProfileDir(), "prism-agent.sock"),
    GLOBAL_SOCKET_PATH,
  ];
}

// PRISM_AGENT_SOCKET=off forces the daemon transport (A/B and stock-Chromium
// runs).
export function kernelTransportEnabled() {
  return process.env.PRISM_AGENT_SOCKET !== "off";
}

export async function connectToKernel() {
  let lastError;
  for (const socketPath of kernelSocketCandidates()) {
    try {
      return await new Promise((resolve, reject) => {
        const socket = net.createConnection(socketPath);
        socket.once("error", reject);
        socket.once("connect", () => resolve(new KernelClient(socket)));
      });
    } catch (error) {
      lastError = error;
    }
  }
  throw lastError ?? new Error("no kernel agent socket reachable");
}

const WIRE_CODE_RE = /^([A-Z][A-Z0-9_]+): ([\s\S]*)$/;

export class KernelClient {
  #socket;
  #nextId = 1;
  #pending = new Map(); // internal id -> { resolve, reject } | { originalId }
  #messageHandlers = new Set(); // raw JSON strings for prism.onCDPMessage
  #sendErrorHandlers = new Set();
  #buffer = "";
  #closed = false;

  constructor(socket) {
    this.#socket = socket;
    socket.setEncoding("utf8");
    socket.on("data", (chunk) => this.#onData(chunk));
    const failAll = () => {
      if (this.#closed) return;
      this.#closed = true;
      for (const entry of this.#pending.values()) {
        entry.reject?.(new Error("kernel agent socket disconnected"));
      }
      this.#pending.clear();
    };
    socket.on("close", failAll);
    socket.on("error", failAll);
  }

  // Verbatim passthrough with id rewriting; fire-and-forget like the daemon's
  // sendCDPMessage. Returns false on a dead socket.
  sendCdp(payload) {
    let request;
    try {
      request = JSON.parse(payload);
    } catch {
      for (const handler of this.#sendErrorHandlers) {
        handler("malformed CDP payload", "PRISM_INVALID_ARGUMENT");
      }
      return false;
    }
    // Contract §6: raw permission grants stay unexposed to agent scripts.
    // Answer locally with a CDP-style error instead of forwarding.
    if (
      request.method === "Browser.grantPermissions" ||
      request.method === "Browser.setPermission"
    ) {
      queueMicrotask(() => {
        const message = JSON.stringify({
          id: request.id,
          error: { message: `${request.method} is not supported for agent scripts` },
        });
        for (const handler of this.#messageHandlers) handler(message);
      });
      return true;
    }
    const internalId = this.#nextId++;
    this.#pending.set(internalId, { originalId: request.id });
    request.id = internalId;
    return this.#write(request);
  }

  // Awaited CDP request (internal callers: snapshot composer, doctor).
  send(method, params = {}, sessionId = undefined) {
    const internalId = this.#nextId++;
    const payload = { id: internalId, method, params };
    if (sessionId) payload.sessionId = sessionId;
    return new Promise((resolve, reject) => {
      this.#pending.set(internalId, { resolve, reject });
      if (!this.#write(payload)) {
        this.#pending.delete(internalId);
        reject(new Error("kernel agent socket write failed"));
      }
    });
  }

  // Prism.* binding call; rejects with {message, code} split from the wire
  // "PRISM_X: detail" prefix.
  async callPrism(name, params = {}) {
    try {
      return await this.send(`Prism.${name}`, params);
    } catch (error) {
      const match = WIRE_CODE_RE.exec(error.message ?? "");
      if (match && match[1].startsWith("PRISM_")) {
        const mapped = new Error(match[2]);
        mapped.code = match[1];
        throw mapped;
      }
      throw error;
    }
  }

  onMessage(handler) {
    this.#messageHandlers.add(handler);
  }

  onSendError(handler) {
    this.#sendErrorHandlers.add(handler);
  }

  close() {
    this.#socket.end();
  }

  #write(object) {
    if (this.#closed) return false;
    try {
      this.#socket.write(JSON.stringify(object) + "\0");
      return true;
    } catch {
      return false;
    }
  }

  #onData(chunk) {
    this.#buffer += chunk;
    let boundary;
    while ((boundary = this.#buffer.indexOf("\0")) !== -1) {
      const raw = this.#buffer.slice(0, boundary);
      this.#buffer = this.#buffer.slice(boundary + 1);
      if (raw) this.#onMessage(raw);
    }
  }

  #onMessage(raw) {
    let message;
    try {
      message = JSON.parse(raw);
    } catch {
      return;
    }
    if (message.id !== undefined) {
      const entry = this.#pending.get(message.id);
      if (entry) {
        this.#pending.delete(message.id);
        if (entry.originalId !== undefined) {
          // Raw-passthrough response: restore the harness's id and forward.
          message.id = entry.originalId;
          const restored = JSON.stringify(message);
          for (const handler of this.#messageHandlers) handler(restored);
        } else if (message.error) {
          const text =
            typeof message.error === "string"
              ? message.error
              : message.error.message || "CDP error";
          entry.reject(new Error(text));
        } else {
          entry.resolve(message.result ?? {});
        }
        return;
      }
      // Unknown id (should not happen): forward verbatim, matching the
      // daemon's broadcast behavior for unrouted messages.
    }
    for (const handler of this.#messageHandlers) handler(raw);
  }
}

// prism.* binding object backed by the kernel domain. Business methods map
// 1:1 onto Prism.* commands; failures resolve {error, error_code} except
// snapshot which rejects (contract §2.2).
export function buildKernelPrismBindings(kernel) {
  const prism = {};

  prism.sendCDPMessage = (payload) => {
    kernel.sendCdp(payload);
  };
  kernel.onMessage((raw) => {
    if (typeof prism.onCDPMessage === "function") prism.onCDPMessage(raw);
  });
  kernel.onSendError((message, code) => {
    if (typeof prism.onSendCDPMessageError === "function") {
      prism.onSendCDPMessageError(message, code);
    }
  });

  const call = async (name, params = {}, unwrap = undefined) => {
    try {
      const result = await kernel.callPrism(name, params);
      return unwrap ? result[unwrap] : result;
    } catch (error) {
      return { error: error.message, error_code: error.code };
    }
  };

  prism.listTaskSpaces = () => call("listTaskSpaces");
  prism.createTaskSpace = (name) => call("createTaskSpace", { name }, "taskSpace");
  prism.useTaskSpace = (id) => call("useTaskSpace", { id }, "taskSpace");
  prism.claimTaskSpace = (id, name) => call("claimTaskSpace", { id, name }, "taskSpace");
  prism.completeTaskSpace = () => call("completeTaskSpace");
  prism.closeTaskSpace = () => call("closeTaskSpace");
  prism.handOffTaskSpace = () => call("handOffTaskSpace", {}, "taskSpace");
  prism.takeOverTaskSpace = () => call("takeOverTaskSpace", {}, "taskSpace");
  prism.showTaskSpace = (id) => call("showTaskSpace", { id });
  prism.getBrowserVersion = () => call("getBrowserVersion");
  prism.listTabs = () => call("listTabs");
  prism.createTab = (url) => call("createTab", { url });

  // Snapshot: the kernel renderer (Phase 3) is the preferred path — it
  // composes the full frame tree including cross-process iframes. The local
  // JS composer (host/src/snapshot.js) remains as fallback for kernels that
  // predate it (detected by the phase-2 stub's error text).
  prism.snapshot = async (options = {}) => {
    try {
      return await kernel.callPrism("snapshot", options ?? {});
    } catch (error) {
      if (!/not yet implemented|was not found|unknown/i.test(error.message)) {
        error.error_code = error.code;
        throw error;
      }
      // Fall through to the JS composer.
    }
    let tabs;
    try {
      ({ tabs } = await kernel.callPrism("listTabs"));
    } catch (error) {
      error.error_code = error.code;
      throw error;
    }
    const active = tabs.find((tab) => tab.active) ?? tabs[0];
    if (!active) {
      // A freshly created space has no tab yet; resolve an empty snapshot so
      // the harness's agent-control probe reads "agent owns an empty space"
      // instead of dying on PRISM_WEB_CONTENTS_UNAVAILABLE.
      return { content: "", refs: [] };
    }
    try {
      return await snapshotTarget(kernel, active.targetId, options);
    } catch (error) {
      const wrapped = new Error(`snapshot failed: ${error.message}`);
      wrapped.error_code = "PRISM_SNAPSHOT_FAILED";
      throw wrapped;
    }
  };

  return prism;
}
