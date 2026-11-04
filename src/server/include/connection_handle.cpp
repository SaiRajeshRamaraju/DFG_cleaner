#include "connection_handle.hpp"

#include "../../network/rpc.hpp"
#include "../../network/sha256.hpp"
#include "../../network/socket_io.hpp"
#include "../chunk_server/chunk_store.hpp"

#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace dfg::protocol;
using namespace dfg::net;
using namespace dfg::storage;

namespace dfg::server {

namespace {

void send_error(int fd, const std::string& msg) {
    auto hdr = make_header(MsgType::ERROR_RESP,
                           static_cast<uint64_t>(msg.size()));
    write_exact(fd, &hdr, sizeof(hdr));
    write_exact(fd, msg.data(), msg.size());
}

void handle_ping(int fd) {
    std::cerr << "[Server] PING received — sending PONG\n";
    auto hdr = make_header(MsgType::PONG, 0);
    auto res = write_exact(fd, &hdr, sizeof(hdr));
    if (!res)
        std::cerr << "[Server] failed to send PONG: " << res.error << "\n";
}

void handle_upload(int fd, uint64_t payload_len, ChunkStore& store) {
    std::cerr << "[Server] UPLOAD_CHUNK: reading " << payload_len << " bytes\n";

    if (payload_len == 0) {
        send_error(fd, "upload payload is empty");
        return;
    }

    constexpr uint64_t MAX_CHUNK = 256ULL * 1024 * 1024;
    if (payload_len > MAX_CHUNK) {
        send_error(fd, "chunk exceeds 256 MiB limit");
        return;
    }

    std::vector<uint8_t> chunk_data;
    auto res = read_into_vec(fd, chunk_data, static_cast<std::size_t>(payload_len));
    if (!res) {
        std::cerr << "[Server] failed to read upload payload: " << res.error << "\n";
        return;
    }

    std::string hash = store.store(chunk_data);
    if (hash.empty()) {
        send_error(fd, "internal storage error");
        return;
    }

    auto ack = make_header(MsgType::UPLOAD_ACK,
                           static_cast<uint64_t>(hash.size()));
    if (auto w = write_exact(fd, &ack, sizeof(ack)); !w) {
        std::cerr << "[Server] failed to send UPLOAD_ACK header: " << w.error << "\n";
        return;
    }
    if (auto w = write_exact(fd, hash.data(), hash.size()); !w) {
        std::cerr << "[Server] failed to send UPLOAD_ACK payload: " << w.error << "\n";
    }

    std::cerr << "[Server] UPLOAD_ACK sent: " << hash << "\n";
}

void handle_download(int fd, uint64_t payload_len, ChunkStore& store) {
    if (payload_len == 0 || payload_len > 256) {
        send_error(fd, "invalid download request payload length");
        return;
    }

    std::vector<uint8_t> hash_buf;
    auto res = read_into_vec(fd, hash_buf, static_cast<std::size_t>(payload_len));
    if (!res) {
        std::cerr << "[Server] failed to read download hash: " << res.error << "\n";
        return;
    }

    std::string hex_hash(hash_buf.begin(), hash_buf.end());
    std::cerr << "[Server] DOWNLOAD_CHUNK: requested hash=" << hex_hash << "\n";

    auto chunk_opt = store.load(hex_hash);
    if (!chunk_opt) {
        send_error(fd, "chunk not found: " + hex_hash);
        return;
    }

    const auto& chunk = *chunk_opt;

    auto hdr = make_header(MsgType::DOWNLOAD_RESP,
                           static_cast<uint64_t>(chunk.size()));
    if (auto w = write_exact(fd, &hdr, sizeof(hdr)); !w) {
        std::cerr << "[Server] failed to send DOWNLOAD_RESP header: " << w.error << "\n";
        return;
    }
    if (auto w = write_exact(fd, chunk.data(), chunk.size()); !w) {
        std::cerr << "[Server] failed to send DOWNLOAD_RESP payload: " << w.error << "\n";
        return;
    }

    std::cerr << "[Server] DOWNLOAD_RESP sent: " << chunk.size() << " bytes\n";
}

} // anonymous namespace

void handle_client_connection(int client_fd, ChunkStore& store) {
    std::cerr << "[Server] new connection on fd=" << client_fd << "\n";

    while (true) {
        MessageHeader hdr{};
        auto res = read_exact(client_fd, &hdr, sizeof(hdr));
        if (!res) {
            std::cerr << "[Server] fd=" << client_fd
                      << " header read: " << res.error << "\n";
            break;
        }

        if (!validate_magic(hdr)) {
            std::cerr << "[Server] bad magic 0x" << std::hex << hdr.magic
                      << std::dec << " on fd=" << client_fd << " — dropping\n";
            send_error(client_fd, "invalid protocol magic");
            break;
        }

        auto type = static_cast<MsgType>(hdr.msg_type);
        std::cerr << "[Server] fd=" << client_fd
                  << " msg=" << msg_type_str(type)
                  << " payload=" << hdr.payload_len << "\n";

        switch (type) {
            case MsgType::PING:
                handle_ping(client_fd);
                break;
            case MsgType::UPLOAD_CHUNK:
                handle_upload(client_fd, hdr.payload_len, store);
                break;
            case MsgType::DOWNLOAD_CHUNK:
                handle_download(client_fd, hdr.payload_len, store);
                break;
            default:
                std::cerr << "[Server] unknown msg_type="
                          << static_cast<int>(hdr.msg_type) << "\n";
                send_error(client_fd, "unknown message type");
                break;
        }
    }

    ::close(client_fd);
    std::cerr << "[Server] fd=" << client_fd << " closed\n";
}

} // namespace dfg::server
