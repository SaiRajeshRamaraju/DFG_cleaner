#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dfg::net {

struct IoResult {
    bool        ok;
    std::string error;

    static IoResult success()                 { return {true,  {}}; }
    static IoResult fail(std::string msg)     { return {false, std::move(msg)}; }
    explicit operator bool() const            { return ok; }
};

IoResult read_exact(int fd, void* buf, std::size_t len);
IoResult write_exact(int fd, const void* buf, std::size_t len);
IoResult read_into_vec(int fd, std::vector<uint8_t>& out, std::size_t len);
IoResult set_nonblocking(int fd);
IoResult set_reuseaddr(int fd);

} // namespace dfg::net
