#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace ll::utf8 {

// Decodes one code point starting at s[i] and advances i. Returns nullopt for
// an invalid sequence, in which case i advances by one byte.
std::optional<char32_t> next(std::string_view s, std::size_t& i);

void append(std::string& out, char32_t cp);

// Decodes a whole string, skipping invalid bytes.
std::u32string decode(std::string_view s);

}  // namespace ll::utf8
