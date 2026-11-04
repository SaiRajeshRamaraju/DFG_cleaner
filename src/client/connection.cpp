#include "connection.hpp"
#include "../network/rpc.hpp"
#include "../network/socket_io.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

using namespace dfg::protocol;
using namespace dfg::net;

// only used by user connection
namespace {
bool read_response_header(int fd, MessageHeader &hdr) {
  auto res = read_exact(fd, &hdr, sizeof(hdr));
  if (!res) {
    std::cerr << "[Client] failed to read response header: " << res.error
              << "\n";
    return false;
  }
  if (!validate_magic(hdr)) {
    std::cerr << "[Client] bad magic in response: 0x" << std::hex << hdr.magic
              << std::dec << "\n";
    return false;
  }
  return true;
}

bool read_response_payload(int fd, const MessageHeader &hdr, std::string &out) {
  if (hdr.payload_len == 0) {
    out.clear();
    return true;
  }
  std::vector<uint8_t> buf;
  auto res = read_into_vec(fd, buf, static_cast<std::size_t>(hdr.payload_len));
  if (!res) {
    std::cerr << "[Client] failed to read response payload: " << res.error
              << "\n";
    return false;
  }
  out.assign(buf.begin(), buf.end());
  return true;
}
} // namespace

int client::connect_to(const std::string &host, int port) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    std::cerr << "[Client] socket(): " << std::strerror(errno) << "\n";
    return -1;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));

  if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
    std::cerr << "[Client] invalid address: " << host << "\n";
    ::close(fd);
    return -1;
  }

  if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    std::cerr << "[Client] connect(): " << std::strerror(errno) << "\n";
    ::close(fd);
    return -1;
  }

  return fd;
}

int client::file_download(const std::string &hex_hash,
                          const std::string &server_ip, int port) {
  std::cerr << "[Client] downloading chunk " << hex_hash << "\n";

  int fd = connect_to(server_ip, port);
  if (fd < 0)
    return 1;

  auto hdr = make_header(MsgType::DOWNLOAD_CHUNK,
                         static_cast<uint64_t>(hex_hash.size()));
  if (auto w = write_exact(fd, &hdr, sizeof(hdr)); !w) {
    std::cerr << "[Client] failed to send download header: " << w.error << "\n";
    ::close(fd);
    return 1;
  }
  if (auto w = write_exact(fd, hex_hash.data(), hex_hash.size()); !w) {
    std::cerr << "[Client] failed to send download hash: " << w.error << "\n";
    ::close(fd);
    return 1;
  }

  MessageHeader resp{};
  if (!read_response_header(fd, resp)) {
    ::close(fd);
    return 1;
  }

  auto resp_type = static_cast<MsgType>(resp.msg_type);

  if (resp_type == MsgType::ERROR_RESP) {
    std::string err_msg;
    read_response_payload(fd, resp, err_msg);
    std::cerr << "[Client] server error: " << err_msg << "\n";
    ::close(fd);
    return 1;
  }

  if (resp_type != MsgType::DOWNLOAD_RESP) {
    std::cerr << "[Client] unexpected response: " << msg_type_str(resp.msg_type)
              << "\n";
    ::close(fd);
    return 1;
  }

  std::vector<uint8_t> chunk;
  if (auto r =
          read_into_vec(fd, chunk, static_cast<std::size_t>(resp.payload_len));
      !r) {
    std::cerr << "[Client] failed to read chunk data: " << r.error << "\n";
    ::close(fd);
    return 1;
  }

  std::string out_file = hex_hash + ".chunk";
  std::ofstream ofs(out_file, std::ios::binary | std::ios::trunc);
  if (!ofs) {
    std::cerr << "[Client] cannot create output file: " << out_file << "\n";
    ::close(fd);
    return 1;
  }
  ofs.write(reinterpret_cast<const char *>(chunk.data()),
            static_cast<std::streamsize>(chunk.size()));
  ofs.close();

  std::cout << "Download OK — " << chunk.size() << " bytes → " << out_file
            << "\n";

  ::close(fd);
  return 0;
}

int client::file_upload(const std::string &file, const std::string &server_ip,
                        int port) {
  std::ifstream ifs(file, std::ios::binary | std::ios::ate);
  if (!ifs) {
    std::cerr << "[Client] cannot open file: " << file << "\n";
    return 1;
  }

  auto size = ifs.tellg();
  ifs.seekg(0, std::ios::beg);

  std::vector<uint8_t> data(static_cast<std::size_t>(size));
  ifs.read(reinterpret_cast<char *>(data.data()), size);
  if (!ifs.good() && !ifs.eof()) {
    std::cerr << "[Client] failed to read file: " << file << "\n";
    return 1;
  }
  ifs.close();

  std::cerr << "[Client] uploading " << file << " (" << data.size()
            << " bytes)\n";

  int fd = connect_to(server_ip, port);
  if (fd < 0)
    return 1;

  auto hdr =
      make_header(MsgType::UPLOAD_CHUNK, static_cast<uint64_t>(data.size()));
  if (auto w = write_exact(fd, &hdr, sizeof(hdr)); !w) {
    std::cerr << "[Client] failed to send upload header: " << w.error << "\n";
    ::close(fd);
    return 1;
  }
  if (auto w = write_exact(fd, data.data(), data.size()); !w) {
    std::cerr << "[Client] failed to send upload payload: " << w.error << "\n";
    ::close(fd);
    return 1;
  }

  MessageHeader resp{};
  if (!read_response_header(fd, resp)) {
    ::close(fd);
    return 1;
  }

  std::string payload;
  if (!read_response_payload(fd, resp, payload)) {
    ::close(fd);
    return 1;
  }

  auto resp_type = static_cast<MsgType>(resp.msg_type);
  if (resp_type == MsgType::UPLOAD_ACK) {
    std::cout << "Upload OK — SHA-256: " << payload << "\n";
  } else if (resp_type == MsgType::ERROR_RESP) {
    std::cerr << "[Client] server error: " << payload << "\n";
    ::close(fd);
    return 1;
  } else {
    std::cerr << "[Client] unexpected response: " << msg_type_str(resp.msg_type)
              << "\n";
    ::close(fd);
    return 1;
  }

  ::close(fd);
  return 0;
}

void client::ping(const std::string &host, int port) {
  int fd = connect_to(host, port);
  if (fd < 0)
    return;

  auto hdr = make_header(MsgType::PING, 0);
  if (auto w = write_exact(fd, &hdr, sizeof(hdr)); !w) {
    std::cerr << "[Client] failed to send PING: " << w.error << "\n";
    ::close(fd);
    return;
  }

  MessageHeader resp{};
  if (!read_response_header(fd, resp)) {
    ::close(fd);
    return;
  }

  if (static_cast<MsgType>(resp.msg_type) == MsgType::PONG) {
    std::cout << "PONG from " << host << ":" << port << "\n";
  } else {
    std::string payload;
    read_response_payload(fd, resp, payload);
    std::cerr << "[Client] unexpected response: " << msg_type_str(resp.msg_type)
              << " " << payload << "\n";
  }

  ::close(fd);
}
