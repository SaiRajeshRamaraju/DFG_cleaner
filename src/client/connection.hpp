#pragma once

#include <cstdint>
#include <string>

namespace client {
// TCP wrapper for client to server connection
int connect_to(const std::string &host, int port);

//
int file_upload(const std::string &file, const std::string &server_ip,
                int port);
int file_download(const std::string &hex_hash, const std::string &server_ip,
                  int port);
void ping(const std::string &host, int port);

} // namespace client
