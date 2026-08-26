// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef PRISM_BROWSER_AGENT_SOCKET_AGENT_SOCKET_SERVER_H_
#define PRISM_BROWSER_AGENT_SOCKET_AGENT_SOCKET_SERVER_H_

#include <atomic>
#include <map>
#include <memory>

#include "base/files/file_path.h"
#include "base/no_destructor.h"

namespace base {
class Thread;
}

namespace content {
class DevToolsPipeHandler;
}

namespace prism {

// Kernel-side agent endpoint: a unix domain socket listener that gives every
// accepted client its own DevTools pipe session.
//
// One DevToolsPipeHandler is created per accepted connection (same NUL-framed
// JSON wire format as --remote-debugging-pipe). Each handler attaches to its
// own browser-target session, so every client gets an isolated DevToolsSession
// and therefore an isolated Prism.* selected-space state — this is what makes
// parallel agent runs on one fork binary safe without the prism-host daemon
// multiplexing a single pipe (ADR-002).
//
// Lifecycle: Start() from BrowserMainLoop::PreMainMessageLoopRun, Stop() from
// ShutdownThreadsAndCleanUp (patch 0002). The listener is always on rather
// than switch-gated: the socket is Prism's primary control channel, and the
// owner-only permissions (0700 directory, 0600 socket file) keep the exposure
// at what the daemon's adapter socket already had. --prism-agent-socket=<path>
// overrides the listen path (tests, multi-profile setups).
class AgentSocketServer {
 public:
  static constexpr char kSocketPathSwitch[] = "prism-agent-socket";

  static AgentSocketServer* GetInstance();

  AgentSocketServer(const AgentSocketServer&) = delete;
  AgentSocketServer& operator=(const AgentSocketServer&) = delete;

  // BrowserThread::UI only.
  void Start();
  void Stop();

  // Resolved listen path: the switch override when present, otherwise
  // ~/Library/Application Support/Prism/agent.sock (POSIX fallback:
  // ~/.prism/agent.sock).
  static base::FilePath SocketPath();

 private:
  AgentSocketServer();
  ~AgentSocketServer();  // via base::NoDestructor
  friend class base::NoDestructor<AgentSocketServer>;

  struct Connection {
    std::unique_ptr<content::DevToolsPipeHandler> handler;
    int fd = -1;
  };

  // Accept thread entry point: poll the listen fd with a timeout so Stop()
  // can join the thread promptly (shutdown() does not wake a blocked accept()
  // on a listening socket — closing it mid-accept is not portable either).
  void AcceptLoop(int listen_fd);
  // UI thread: wraps the accepted fd in a DevToolsPipeHandler session.
  void OnConnectionAccepted(int fd);
  // UI thread: pipe handler disconnect callback; tears the session down.
  void OnClientDisconnected(int connection_id);

  std::unique_ptr<base::Thread> accept_thread_;
  std::atomic<bool> stop_accept_loop_{false};
  int listen_fd_ = -1;
  base::FilePath socket_path_;
  std::map<int, Connection> connections_;
  int next_connection_id_ = 1;
};

}  // namespace prism

#endif  // PRISM_BROWSER_AGENT_SOCKET_AGENT_SOCKET_SERVER_H_
