#pragma once
#include <iostream>
namespace client {

int file_upload(const std::string &file, const std::string &server_ip,
                int port) {

  return 0;
}

int file_download(const std::string &file, const std::string &server_ip,
                  int port);

void ping(const std::string &host, int port);
} // namespace client
