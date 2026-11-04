#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace ll {

// Splits one page's normalized text into chunks of at most `max_chars` bytes,
// breaking on line boundaries. A slide usually fits in one chunk, which keeps
// citations at page granularity. A single line longer than `max_chars` is
// split on a UTF-8 code point boundary.
std::vector<std::string> chunk_page_text(const std::string& text, std::size_t max_chars);

}  // namespace ll
