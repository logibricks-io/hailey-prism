// CLI-side daemon client + `prism` binding object factory.
//
// The returned object implements the binding contract
// (docs/binding-contract.md) by remoting every method to the daemon:
//   - sendCDPMessage stays fire-and-forget; responses and events come back as
//     socket events and are handed to the harness-assigned onCDPMessage.
//   - Business methods resolve their result; failures arrive as
//     {error:{message, code}} and are reshaped per method: snapshot rejects,
//     everything else resolves {error, error_code}.

import net from "node:net";

export function connectToDaemon(socketPath) {
  return new Promise((resolve, reject) => {
    const socket = net.createConnection(socketPath);
    const client = new DaemonClient(socket);
    socket.once("error", reject);
    socket.once("connect", () => resolve(client));
  });
}

export class DaemonClient {
  #socket;
  #nextId = 1;
  #pending = new Map();
  #eventHandlers = new Set();

  constructor(socket) {
    this.#socket = socket;
    socket.setEncoding("utf8");
    let buffer = "";
    socket.on("data", (chunk) => {
      buffer += chunk;
      let boundary;
      while ((boundary = buffer.indexOf("\n")) !== -1) {
        const line = buffer.slice(0, boundary);
        buffer = buffer.slice(boundary + 1);
        if (line.trim()) this.#onMessage(line);
      }
    });
    const failAll = () => {
      for (const { reject } of this.#pending.values()) {
        reject(new Error("prism-host daemon disconnected"));
      }
      this.#pending.clear();
    };
    socket.on("close", failAll);
    socket.on("error", failAll);
  }

  call(method, params = {}) {
    const id = this.#nextId++;
    return new Promise((resolve, reject) => {
      this.#pending.set(id, { resolve, reject });
      this.#socket.write(JSON.stringify({ kind: "call", id, method, params }) + "\n");
    });
  }

  sendCdp(payload) {
    this.#socket.write(JSON.stringify({ kind: "cdp", payload }) + "\n");
  }

  onEvent(handler) {
    this.#eventHandlers.add(handler);
  }

  close() {
    this.#socket.end();
  }

  #onMessage(line) {
    let message;
    try {
      message = JSON.parse(line);
    } catch {
      return;
    }
    if (message.id !== undefined) {
      const entry = this.#pending.get(message.id);
      if (!entry) return;
      this.#pending.delete(message.id);
      if (message.error) {
        const error = new Error(message.error.message);
        error.code = message.error.code;
        entry.reject(error);
      } else {
        entry.resolve(message.result);
      }
      return;
    }
    if (message.event) {
      for (const handler of this.#eventHandlers) handler(message);
    }
  }
}

// Methods whose failures must reject (contract §2.2). Everything else resolves
// {error, error_code} so the harness's assertNoEgoError path sees them.
const REJECTING_METHODS = new Set(["snapshot"]);

export function buildPrismBindings(client) {
  const prism = {};

  prism.sendCDPMessage = (payload) => {
    client.sendCdp(payload);
  };

  client.onEvent((event) => {
    if (event.event === "cdp" && typeof prism.onCDPMessage === "function") {
      prism.onCDPMessage(event.message);
    }
    if (event.event === "cdpSendError" && typeof prism.onSendCDPMessageError === "function") {
      prism.onSendCDPMessageError(event.message, event.code);
    }
  });

  // Each binding method packs its positional args into the daemon's params
  // object shape (contract §2).
  const SIGNATURES = {
    listTabs: () => ({}),
    createTab: (url) => ({ url }),
    snapshot: (options) => options ?? {},
    listTaskSpaces: () => ({}),
    useTaskSpace: (id) => ({ id }),
    createTaskSpace: (name) => ({ name }),
    claimTaskSpace: (id, name) => ({ id, name }),
    completeTaskSpace: () => ({}),
    closeTaskSpace: () => ({}),
    handOffTaskSpace: () => ({}),
    takeOverTaskSpace: () => ({}),
    showTaskSpace: (id) => ({ id }),
    getBrowserVersion: () => ({}),
  };

  const remote = (name) => {
    const pack = SIGNATURES[name];
    return async (...args) => {
      try {
        return await client.call(name, pack(...args));
      } catch (error) {
        if (REJECTING_METHODS.has(name)) {
          error.error_code = error.code;
          throw error;
        }
        return { error: error.message, error_code: error.code };
      }
    };
  };

  for (const name of Object.keys(SIGNATURES)) {
    prism[name] = remote(name);
  }

  return prism;
}
