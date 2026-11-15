#include "socket_io.hpp"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <format>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
dfg::net::socket_io::socket_io(const std::string &ip, int port)
    : fd(socket(AF_INET, SOCK_STREAM, 0)) {
  if (fd < 0) {
    throw std::runtime_error("Socket creation failed error.");
  }
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);

  if (inet_pton(AF_INET, ip.c_str(), &server_addr.sin_family) <= 0) {
    close(fd);
    throw std::runtime_error("Invalid Ip address");
  }
}

dfg::net::socket_io::~socket_io() {
  if (fd >= 0) {
    close(fd);
  }
}

int dfg::net::socket_io::connect_to() {
  if (connect(fd, reinterpret_cast<struct sockaddr *>(&server_addr),
              sizeof(server_addr)) < 0) {
    std::cerr << "Connection to server failed" << std::endl;
    return -1;
  }

  return fd;
}

int dfg::net::socket_io::server_listen() {
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  if (bind(fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    std::cerr << "Socket bind failed" << std::endl;
    return -1;
  }
  if (listen(fd, SOMAXCONN) < 0) {
    std::cerr << "Socket listen failed" << std::endl;
    return -1;
  }

  return fd;
}
