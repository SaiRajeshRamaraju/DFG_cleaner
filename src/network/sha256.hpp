#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dfg::crypto {

constexpr std::size_t SHA256_DIGEST_LEN = 32;
using Sha256Digest = std::array<uint8_t, SHA256_DIGEST_LEN>;

Sha256Digest sha256(const void* data, std::size_t len);
Sha256Digest sha256(const std::vector<uint8_t>& data);
std::string digest_to_hex(const Sha256Digest& digest);
std::string sha256_hex(const void* data, std::size_t len);
std::string sha256_hex(const std::vector<uint8_t>& data);

} // namespace dfg::crypto
