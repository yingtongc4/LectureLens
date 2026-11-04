#include "core/utf8.hpp"

namespace ll::utf8 {

std::optional<char32_t> next(std::string_view s, std::size_t& i) {
    const auto b0 = static_cast<unsigned char>(s[i]);
    int len = 0;
    char32_t cp = 0;
    if (b0 < 0x80) {
        ++i;
        return b0;
    } else if ((b0 & 0xE0) == 0xC0) {
        len = 2;
        cp = b0 & 0x1F;
    } else if ((b0 & 0xF0) == 0xE0) {
        len = 3;
        cp = b0 & 0x0F;
    } else if ((b0 & 0xF8) == 0xF0) {
        len = 4;
        cp = b0 & 0x07;
    } else {
        ++i;
        return std::nullopt;
    }
    if (i + len > s.size()) {
        ++i;
        return std::nullopt;
    }
    for (int k = 1; k < len; ++k) {
        const auto b = static_cast<unsigned char>(s[i + k]);
        if ((b & 0xC0) != 0x80) {
            ++i;
            return std::nullopt;
        }
        cp = (cp << 6) | (b & 0x3F);
    }
    i += len;
    return cp;
}

void append(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::u32string decode(std::string_view s) {
    std::u32string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        if (const auto cp = next(s, i)) out += *cp;
    }
    return out;
}

}  // namespace ll::utf8
