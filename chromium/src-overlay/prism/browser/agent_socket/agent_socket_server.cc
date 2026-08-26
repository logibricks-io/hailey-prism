// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "prism/browser/agent_socket/agent_socket_server.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <utility>

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/posix/eintr_wrapper.h"
#include "base/threading/thread.h"
#include "build/build_config.h"
#include "content/browser/devtools/devtools_pipe_handler.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"

namespace prism {

// static
AgentSocketServer* AgentSocketServer::GetInstance() {
  static base::NoDestructor<AgentSocketServer> instance;
  return instance.get();
}

AgentSocketServer::AgentSocketServer() = default;
AgentSocketServer::~AgentSocketServer() = default;

// static
base::FilePath AgentSocketServer::SocketPath() {
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  if (command_line.HasSwitch(kSocketPathSwitch)) {
    return command_line.GetSwitchValuePath(kSocketPathSwitch);
  }
  base::FilePath home;
  if (!base::PathService::Get(base::DIR_HOME, &home)) {
    return base::FilePath();
  }
#if BUILDFLAG(IS_MAC)
  return home.Append("Library")
      .Append("Application Support")
      .Append("Prism")
      .Append("agent.sock");
#else
  return home.Append(".prism").Append("agent.sock");
#endif
}

void AgentSocketServer::Start() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (listen_fd_ >= 0) {
    return;  // already running
  }

  socket_path_ = SocketPath();
  if (socket_path_.empty()) {
    LOG(ERROR) << "prism agent socket: could not resolve a listen path";
    return;
  }
  const std::string& path = socket_path_.value();
  if (path.size() >= sizeof(sockaddr_un::sun_path)) {
    LOG(ERROR) << "prism agent socket: path too long: " << path;
    return;
  }

  // Owner-only directory and socket file: the listener accepts full DevTools
  // sessions, so it must not be reachable by other users.
  if (!base::CreateDirectory(socket_path_.DirName())) {
    LOG(ERROR) << "prism agent socket: cannot create directory "
               << socket_path_.DirName().value();
    return;
  }
  chmod(socket_path_.DirName().value().c_str(), 0700);

  // A stale socket file from a crashed browser blocks bind(); the listener is
  // single-instance per user profile, so replacing it is safe.
  unlink(path.c_str());

  const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    PLOG(ERROR) << "prism agent socket: socket()";
    return;
  }
  fcntl(fd, F_SETFD, FD_CLOEXEC);

  sockaddr_un addr = {};
  addr.sun_family = AF_UNIX;
  path.copy(addr.sun_path, sizeof(addr.sun_path) - 1, 0);

  if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    PLOG(ERROR) << "prism agent socket: bind(" << path << ")";
    close(fd);
    return;
  }
  chmod(path.c_str(), 0600);
  if (listen(fd, /*backlog=*/16) != 0) {
    PLOG(ERROR) << "prism agent socket: listen()";
    close(fd);
    unlink(path.c_str());
    return;
  }
  listen_fd_ = fd;
  // Non-blocking: the accept loop polls with a timeout so Stop() can join the
  // thread promptly (shutdown() does not wake a blocked accept() on a
  // listening socket, and close() mid-accept is not portable).
  int flags = fcntl(fd, F_GETFL);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);

  auto thread = std::make_unique<base::Thread>("PrismAgentSocketAccept");
  if (!thread->Start()) {
    LOG(ERROR) << "prism agent socket: failed to start the accept thread";
    close(listen_fd_);
    listen_fd_ = -1;
    unlink(path.c_str());
    return;
  }
  accept_thread_ = std::move(thread);
  accept_thread_->task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&AgentSocketServer::AcceptLoop,
                                base::Unretained(this), listen_fd_));
  VLOG(1) << "prism agent socket: listening on " << path;
}

void AgentSocketServer::Stop() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  connections_.clear();  // DevToolsPipeHandler dtors shut their threads down.

  stop_accept_loop_.store(true);
  if (listen_fd_ >= 0) {
    close(listen_fd_);
    listen_fd_ = -1;
  }
  if (accept_thread_) {
    accept_thread_->Stop();  // joins within one poll interval
    accept_thread_.reset();
  }
  if (!socket_path_.empty()) {
    unlink(socket_path_.value().c_str());
    socket_path_.clear();
  }
}

void AgentSocketServer::AcceptLoop(int listen_fd) {
  while (!stop_accept_loop_.load()) {
    pollfd pfd = {.fd = listen_fd, .events = POLLIN, .revents = 0};
    const int ready = HANDLE_EINTR(poll(&pfd, 1, /*timeout_ms=*/200));
    if (ready == 0) {
      continue;  // timeout: re-check the stop flag
    }
    if (ready < 0 || (pfd.revents & (POLLERR | POLLNVAL))) {
      return;  // Stop() closed the listener
    }
    if (!(pfd.revents & POLLIN)) {
      continue;
    }
    const int fd = HANDLE_EINTR(accept(listen_fd, nullptr, nullptr));
    if (fd < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ECONNABORTED) {
        continue;  // raced with another accept / client hung up early
      }
      PLOG(ERROR) << "prism agent socket: accept()";
      return;
    }
    // Keep the fd out of child processes (renderers), and force it back to
    // blocking mode: on BSD/macOS accept() inherits O_NONBLOCK from the
    // (polled) listener, and DevToolsPipeHandler's reader thread does
    // blocking read()s that would instantly fail with EAGAIN.
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    const int acc_flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, acc_flags & ~O_NONBLOCK);
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE, base::BindOnce(&AgentSocketServer::OnConnectionAccepted,
                                  base::Unretained(this), fd));
  }
}

void AgentSocketServer::OnConnectionAccepted(int fd) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (listen_fd_ < 0) {
    close(fd);  // stopping between accept and handoff
    return;
  }
  const int id = next_connection_id_++;
  Connection connection;
  connection.fd = fd;
  // A connected socket is both the read and the write end; DevToolsPipeHandler
  // runs its blocking IO on dedicated threads exactly like the startup pipe.
  connection.handler = std::make_unique<content::DevToolsPipeHandler>(
      fd, fd,
      base::BindOnce(&AgentSocketServer::OnClientDisconnected,
                     base::Unretained(this), id));
  connections_.emplace(id, std::move(connection));
}

void AgentSocketServer::OnClientDisconnected(int connection_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto it = connections_.find(connection_id);
  if (it == connections_.end()) {
    return;
  }
  // DevToolsPipeHandler shutdown()s its fds but never close()s them (the
  // startup pipe fds are process-global); accepted fds are ours to close.
  const int fd = it->second.fd;
  connections_.erase(it);
  close(fd);
}

}  // namespace prism
