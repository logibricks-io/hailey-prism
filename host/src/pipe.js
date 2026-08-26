// NUL-framed JSON message channel over two file descriptors.
//
// Chromium's --remote-debugging-pipe protocol: the browser reads commands from
// its fd 3 and writes responses/events to its fd 4, each message a single JSON
// payload terminated by a NUL byte. We create both pipes at spawn time; the
// parent ends arrive as child.stdio[3] (our write side) and stdio[4] (our read
// side).

export class PipeTransport {
  #writeStream;
  #readStream;
  #buffer = "";
  #messageHandler = null;
  #closeHandler = null;

  constructor({ writeStream, readStream }) {
    this.#writeStream = writeStream;
    this.#readStream = readStream;
    this.#readStream.setEncoding("utf8");
    this.#readStream.on("data", (chunk) => this.#onData(chunk));
    this.#readStream.on("error", () => this.#closeHandler?.());
    this.#readStream.on("end", () => this.#closeHandler?.());
  }

  onMessage(handler) {
    this.#messageHandler = handler;
  }

  onClose(handler) {
    this.#closeHandler = handler;
  }

  // Fire-and-forget by design: mirrors the synchronous sendCDPMessage contract.
  // Returns false when the underlying socket reports a dead browser.
  send(payload) {
    try {
      this.#writeStream.write(payload + "\0");
      return true;
    } catch {
      return false;
    }
  }

  #onData(chunk) {
    this.#buffer += chunk;
    let boundary;
    while ((boundary = this.#buffer.indexOf("\0")) !== -1) {
      const message = this.#buffer.slice(0, boundary);
      this.#buffer = this.#buffer.slice(boundary + 1);
      if (message) this.#messageHandler?.(message);
    }
  }
}
