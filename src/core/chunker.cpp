#include "core/chunker.hpp"

#include <sstream>

namespace ll {
namespace {

// Largest cut <= limit that does not land inside a multi-byte sequence.
std::size_t utf8_safe_cut(const std::string& s, std::size_t limit) {
    if (limit >= s.size()) return s.size();
    std::size_t cut = limit;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    return cut == 0 ? limit : cut;
}

}  // namespace

std::vector<std::string> chunk_page_text(const std::string& text, std::size_t max_chars) {
    std::vector<std::string> chunks;
    if (text.empty() || max_chars == 0) return chunks;

    std::string current;
    auto flush = [&] {
        if (!current.empty()) chunks.push_back(std::move(current));
        current.clear();
    };

    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        while (line.size() > max_chars) {
            flush();
            const auto cut = utf8_safe_cut(line, max_chars);
            chunks.push_back(line.substr(0, cut));
            line.erase(0, cut);
        }
        const std::size_t extra = current.empty() ? line.size() : line.size() + 1;
        if (current.size() + extra > max_chars) flush();
        if (!current.empty()) current += '\n';
        current += line;
    }
    flush();
    return chunks;
}

}  // namespace ll
