#include "socket_io.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace dfg::net {

IoResult read_exact(int fd, void* buf, std::size_t len) {
    auto* ptr = static_cast<uint8_t*>(buf);
    std::size_t remaining = len;

    while (remaining > 0) {
        ssize_t n = ::recv(fd, ptr, remaining, 0);
        if (n > 0) {
            ptr       += static_cast<std::size_t>(n);
            remaining -= static_cast<std::size_t>(n);
        } else if (n == 0) {
            return IoResult::fail("peer closed connection (" +
                                  std::to_string(len - remaining) + "/" +
                                  std::to_string(len) + " bytes read)");
        } else {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            return IoResult::fail(std::string("recv: ") + std::strerror(errno));
        }
    }
    return IoResult::success();
}

IoResult write_exact(int fd, const void* buf, std::size_t len) {
    const auto* ptr = static_cast<const uint8_t*>(buf);
    std::size_t remaining = len;

    while (remaining > 0) {
        ssize_t n = ::send(fd, ptr, remaining, MSG_NOSIGNAL);
        if (n > 0) {
            ptr       += static_cast<std::size_t>(n);
            remaining -= static_cast<std::size_t>(n);
        } else if (n == 0) {
            continue;
        } else {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            return IoResult::fail(std::string("send: ") + std::strerror(errno));
        }
    }
    return IoResult::success();
}

IoResult read_into_vec(int fd, std::vector<uint8_t>& out, std::size_t len) {
    out.resize(len);
    return read_exact(fd, out.data(), len);
}

IoResult set_nonblocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return IoResult::fail(std::string("fcntl F_GETFL: ") + std::strerror(errno));
    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        return IoResult::fail(std::string("fcntl F_SETFL: ") + std::strerror(errno));
    return IoResult::success();
}

IoResult set_reuseaddr(int fd) {
    int opt = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
        return IoResult::fail(std::string("setsockopt SO_REUSEADDR: ") + std::strerror(errno));
    return IoResult::success();
}

} // namespace dfg::net
