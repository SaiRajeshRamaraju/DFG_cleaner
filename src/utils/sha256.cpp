#include "sha256.hpp"

#include <cstdio>
#include <stdexcept>
#include <openssl/evp.h>

namespace dfg::crypto {

Sha256Digest sha256(const void* data, std::size_t len) {
    Sha256Digest digest{};
    unsigned int digest_len = 0;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx)
        throw std::runtime_error("EVP_MD_CTX_new failed");

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1 ||
        EVP_DigestUpdate(ctx, data, len)              != 1 ||
        EVP_DigestFinal_ex(ctx, digest.data(), &digest_len) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("SHA-256 computation failed");
    }

    EVP_MD_CTX_free(ctx);
    return digest;
}

Sha256Digest sha256(const std::vector<uint8_t>& data) {
    return sha256(data.data(), data.size());
}

std::string digest_to_hex(const Sha256Digest& digest) {
    std::string hex;
    hex.reserve(SHA256_DIGEST_LEN * 2);
    for (uint8_t byte : digest) {
        char buf[3];
        std::snprintf(buf, sizeof(buf), "%02x", byte);
        hex.append(buf, 2);
    }
    return hex;
}

std::string sha256_hex(const void* data, std::size_t len) {
    return digest_to_hex(sha256(data, len));
}

std::string sha256_hex(const std::vector<uint8_t>& data) {
    return sha256_hex(data.data(), data.size());
}

} // namespace dfg::crypto
