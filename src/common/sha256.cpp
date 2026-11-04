#include "common/sha256.hpp"

#include <openssl/evp.h>

#include <array>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace ll {

std::string sha256_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path.string());

    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
    if (!ctx || EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1) {
        throw std::runtime_error("EVP_DigestInit_ex failed");
    }

    std::array<char, 1 << 16> buf{};
    while (in) {
        in.read(buf.data(), buf.size());
        if (const auto n = in.gcount(); n > 0) {
            EVP_DigestUpdate(ctx.get(), buf.data(), static_cast<std::size_t>(n));
        }
    }

    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int len = 0;
    EVP_DigestFinal_ex(ctx.get(), digest.data(), &len);

    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (unsigned int i = 0; i < len; ++i) {
        out += kHex[digest[i] >> 4];
        out += kHex[digest[i] & 0xF];
    }
    return out;
}

}  // namespace ll
