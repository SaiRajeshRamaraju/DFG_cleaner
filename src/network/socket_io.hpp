#pragma once

#include <arpa/inet.h>
#include <cstddef>
#include <cstdint>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <vector>

namespace dfg::net {

// This abstraction help to use linux socket api little simlper for me.
// I am passing address and port that need to connect to or run server on as
// part of the constructor . I am still learning and hand writing majority of
// this code so this maybe wrong or right. TODO: Need refactoring in future.
class socket_io {
private:
  int fd;
  int opt = 1;

public:
  sockaddr_in server_addr;
  socket_io(const std::string &ip, int port);
  ~socket_io();
  sockaddr_in client_addr{};
  socklen_t client_len;

  // Prevent accidental copying(so no two objects closes the same socket)

  socket_io(const socket_io &) = delete;
  socket_io &operator=(const socket_io &) = delete;

  int connect_to();
  int server_listen();
};
} // namespace dfg::net
