#pragma once

#include <filesystem>
#include <string>

namespace ll {

// Lowercase hex SHA-256 of a file's contents, streamed in blocks.
std::string sha256_file(const std::filesystem::path& path);

}  // namespace ll
