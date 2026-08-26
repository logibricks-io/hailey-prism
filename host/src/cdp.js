// Browser-level CDP connection over the pipe transport.
//
// One daemon owns exactly one CdpConnection to one browser process. All client
// traffic is multiplexed through it with globally-rewritten request ids, so
// per-client id sequences can never collide on the shared pipe.

export class CdpConnection {
  #transport;
  #nextId = 1;
  #pending = new Map(); // globalId -> { resolve, reject, context }
  #eventHandlers = new Set();
  #closed = false;

  constructor(transport) {
    this.#transport = transport;
    transport.onMessage((raw) => this.#onMessage(raw));
    transport.onClose(() => this.#failAll(new Error("browser pipe closed")));
  }

  get closed() {
    return this.#closed;
  }

  // context is an opaque value the daemon uses to route the response back to
  // the originating client (e.g. { clientId, localId, method }).
  send(method, params = {}, sessionId = undefined, context = undefined) {
    const id = this.#nextId++;
    const payload = { id, method, params };
    if (sessionId) payload.sessionId = sessionId;
    return new Promise((resolve, reject) => {
      this.#pending.set(id, { resolve, reject, context });
      const ok = this.#transport.send(JSON.stringify(payload));
      if (!ok) {
        this.#pending.delete(id);
        reject(new Error("browser pipe write failed"));
      }
    });
  }

  onEvent(handler) {
    this.#eventHandlers.add(handler);
    return () => this.#eventHandlers.delete(handler);
  }

  #onMessage(raw) {
    let message;
    try {
      message = JSON.parse(raw);
    } catch {
      return; // malformed browser output; drop
    }
    if (message.id !== undefined) {
      const entry = this.#pending.get(message.id);
      if (!entry) return;
      this.#pending.delete(message.id);
      if (message.error) {
        const text =
          typeof message.error === "string"
            ? message.error
            : message.error.message || "CDP error";
        const error = new Error(text);
        error.cdpError = message.error;
        entry.reject(error);
      } else {
        entry.resolve(message.result ?? {});
      }
      return;
    }
    if (message.method) {
      for (const handler of this.#eventHandlers) handler(message);
    }
  }

  #failAll(error) {
    if (this.#closed) return;
    this.#closed = true;
    for (const entry of this.#pending.values()) entry.reject(error);
    this.#pending.clear();
  }
}
