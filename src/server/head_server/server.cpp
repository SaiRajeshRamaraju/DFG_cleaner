#include "../../network/socket_io.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sys/epoll.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace dfg::head_server {

constexpr int MAX_EVENTS =
    1024; // NOTE: THIS CAN BE CHANGES TO BE AN ENV VARIABLE

// Stores per-connection state since functions no longer block on a single
// client
struct ClientSession {
  int fd;
  std::string read_buffer;
  std::string write_buffer;
};

// Helper to set any file descriptor to non-blocking mode
bool set_non_blocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1)
    return false;
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

void close_connection(int epoll_fd, int client_fd,
                      std::unordered_map<int, ClientSession> &sessions) {
  epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
  close(client_fd);
  sessions.erase(client_fd);
}

void handle_client_read(int epoll_fd, ClientSession &session,
                        std::unordered_map<int, ClientSession> &sessions) {
  char buf[4096];
  while (true) {
    ssize_t bytes_read = read(session.fd, buf, sizeof(buf));
    if (bytes_read > 0) {
      session.read_buffer.append(buf, bytes_read);
      // TODO: Parse complete messages from session.read_buffer here.
      // If you have a response ready, queue it in session.write_buffer
      // and modify epoll to watch for EPOLLOUT.
    } else if (bytes_read == 0) {
      // Client cleanly closed the connection
      close_connection(epoll_fd, session.fd, sessions);
      return;
    } else {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // All available data has been read for now; return to event loop
        break;
      }
      // Actual socket error
      close_connection(epoll_fd, session.fd, sessions);
      return;
    }
  }
}

int run_head_server(int port) {
  auto socket_c = dfg::net::socket_io("127.0.0.1", port);
  int server_listener = socket_c.server_listen();
  if (server_listener == -1) {
    return -1;
  }

  // 1. Make the listening socket non-blocking
  if (!set_non_blocking(server_listener)) {
    std::cerr << "Failed to set listener non-blocking\n";
    close(server_listener);
    return -1;
  }

  // 2. Create the epoll instance
  int epoll_fd = epoll_create1(0);
  if (epoll_fd == -1) {
    std::cerr << "epoll_create1() failed: " << std::strerror(errno) << "\n";
    close(server_listener);
    return -1;
  }

  // 3. Register the listening socket with epoll
  epoll_event ev{};
  ev.events = EPOLLIN; // Watch for incoming connections
  ev.data.fd = server_listener;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_listener, &ev) == -1) {
    std::cerr << "epoll_ctl() failed: " << std::strerror(errno) << "\n";
    close(epoll_fd);
    close(server_listener);
    return -1;
  }

  std::vector<epoll_event> events(MAX_EVENTS);
  std::unordered_map<int, ClientSession> sessions;

  // 4. Main Event Loop
  while (true) {
    int num_events = epoll_wait(epoll_fd, events.data(), MAX_EVENTS, -1);
    if (num_events < 0) {
      if (errno == EINTR)
        continue; // Interrupted by signal, retry
      std::cerr << "epoll_wait() failed: " << std::strerror(errno) << "\n";
      break;
    }

    for (int i = 0; i < num_events; ++i) {
      int active_fd = events[i].data.fd;

      // Case A: New incoming connection(s) on the listener socket
      if (active_fd == server_listener) {
        while (true) {
          sockaddr_in client_addr{};
          socklen_t addr_len = sizeof(client_addr);
          int client_fd =
              accept(server_listener,
                     reinterpret_cast<sockaddr *>(&client_addr), &addr_len);
          if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              // All pending connections accepted
              break;
            }
            std::cerr << "accept() failed: " << std::strerror(errno) << "\n";
            break;
          }

          set_non_blocking(client_fd);

          // Register new client with epoll (Edge-Triggered + Read)
          epoll_event client_ev{};
          client_ev.events = EPOLLIN | EPOLLET;
          client_ev.data.fd = client_fd;
          if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_ev) == -1) {
            close(client_fd);
            continue;
          }

          sessions[client_fd] = ClientSession{client_fd, "", ""};
        }
      }
      // Case B: Existing client has data to read or disconnected
      else {
        if (events[i].events & (EPOLLERR | EPOLLHUP)) {
          close_connection(epoll_fd, active_fd, sessions);
          continue;
        }

        if (events[i].events & EPOLLIN) {
          auto it = sessions.find(active_fd);
          if (it != sessions.end()) {
            handle_client_read(epoll_fd, it->second, sessions);
          }
        }
      }
    }
  }

  close(epoll_fd);
  close(server_listener);
  return 0;
}

} // namespace dfg::head_server
