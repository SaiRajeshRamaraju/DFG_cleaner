#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace dfg::storage {

class ChunkStore {
public:
    explicit ChunkStore(std::filesystem::path base_dir);

    std::string store(const std::vector<uint8_t>& data);
    std::string store(const void* data, std::size_t len);
    std::optional<std::vector<uint8_t>> load(const std::string& hex_hash) const;
    bool exists(const std::string& hex_hash) const;
    std::filesystem::path chunk_path(const std::string& hex_hash) const;

private:
    std::filesystem::path base_dir_;
};

} // namespace dfg::storage
