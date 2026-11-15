#include "socket_io.hpp"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>

namespace dfg::net {

int connect_to(const std::string &ip, int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    std::cerr << " Connection to server failed" << std::endl;
    return -1;
  }
  return fd;
}

} // namespace dfg::net
