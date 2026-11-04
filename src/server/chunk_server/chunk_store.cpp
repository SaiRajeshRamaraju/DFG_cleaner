#include "chunk_store.hpp"
#include "../../network/sha256.hpp"

#include <fstream>
#include <iostream>
#include <iterator>

namespace dfg::storage {

ChunkStore::ChunkStore(std::filesystem::path base_dir)
    : base_dir_(std::move(base_dir)) {
    std::filesystem::create_directories(base_dir_);
    std::cerr << "[ChunkStore] initialized at " << base_dir_.string() << "\n";
}

std::filesystem::path ChunkStore::chunk_path(const std::string& hex_hash) const {
    return base_dir_ / hex_hash.substr(0, 2) / hex_hash;
}

std::string ChunkStore::store(const void* data, std::size_t len) {
    std::string hash = dfg::crypto::sha256_hex(data, len);
    auto path = chunk_path(hash);

    if (std::filesystem::exists(path)) {
        std::cerr << "[ChunkStore] dedup hit: " << hash << " (" << len << " bytes)\n";
        return hash;
    }

    std::filesystem::create_directories(path.parent_path());

    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        std::cerr << "[ChunkStore] ERROR: failed to open " << path << " for writing\n";
        return {};
    }

    ofs.write(static_cast<const char*>(data), static_cast<std::streamsize>(len));
    ofs.flush();

    if (!ofs.good()) {
        std::cerr << "[ChunkStore] ERROR: write failed for " << path << "\n";
        return {};
    }

    std::cerr << "[ChunkStore] stored " << hash << " (" << len << " bytes)\n";
    return hash;
}

std::string ChunkStore::store(const std::vector<uint8_t>& data) {
    return store(data.data(), data.size());
}

std::optional<std::vector<uint8_t>> ChunkStore::load(const std::string& hex_hash) const {
    auto path = chunk_path(hex_hash);

    if (!std::filesystem::exists(path))
        return std::nullopt;

    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs) {
        std::cerr << "[ChunkStore] ERROR: failed to open " << path << " for reading\n";
        return std::nullopt;
    }

    auto size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    std::vector<uint8_t> buf(static_cast<std::size_t>(size));
    ifs.read(reinterpret_cast<char*>(buf.data()), size);

    if (!ifs.good() && !ifs.eof()) {
        std::cerr << "[ChunkStore] ERROR: read failed for " << path << "\n";
        return std::nullopt;
    }

    return buf;
}

bool ChunkStore::exists(const std::string& hex_hash) const {
    return std::filesystem::exists(chunk_path(hex_hash));
}

} // namespace dfg::storage
