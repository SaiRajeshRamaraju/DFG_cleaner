#include "../../network/socket_io.hpp"
#include "../include/connection_handle.hpp"
#include "chunk_store.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace dfg::cluster_server {

int run_chunk_server(uint16_t port, const std::string &storage_dir) {
  dfg::storage::ChunkStore store(storage_dir);

  int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    std::cerr << "[Server] socket(): " << std::strerror(errno) << "\n";
    return 1;
  }

  if (auto r = dfg::net::set_reuseaddr(listen_fd); !r) {
    std::cerr << "[Server] " << r.error << "\n";
    ::close(listen_fd);
    return 1;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  if (::bind(listen_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) <
      0) {
    std::cerr << "[Server] bind(): " << std::strerror(errno) << "\n";
    ::close(listen_fd);
    return 1;
  }

  constexpr int BACKLOG = 64;
  if (::listen(listen_fd, BACKLOG) < 0) {
    std::cerr << "[Server] listen(): " << std::strerror(errno) << "\n";
    ::close(listen_fd);
    return 1;
  }

  std::cerr << "[Server] chunk server listening on port " << port
            << "  storage=" << storage_dir << "\n";

  while (true) {
    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);

    int client_fd = ::accept(
        listen_fd, reinterpret_cast<sockaddr *>(&client_addr), &client_len);
    if (client_fd < 0) {
      if (errno == EINTR)
        continue;
      std::cerr << "[Server] accept(): " << std::strerror(errno) << "\n";
      continue;
    }

    char ip_str[INET_ADDRSTRLEN]{};
    ::inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
    std::cerr << "[Server] accepted connection from " << ip_str << ":"
              << ntohs(client_addr.sin_port) << " (fd=" << client_fd << ")\n";

    std::thread([client_fd, &store]() {
      handle_client_connection(client_fd, store);
    }).detach();
  }

  ::close(listen_fd);
  return 0;
}
void server(int port) {}

} // namespace dfg::cluster_server
//
//
//
