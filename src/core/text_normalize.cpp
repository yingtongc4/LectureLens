#include "core/text_normalize.hpp"

#include <vector>

#include "core/utf8.hpp"

namespace ll {
namespace {

bool is_bullet_glyph(char32_t cp) {
    // U+00B7 is how OCR usually reads a round bullet.
    return (cp >= 0xE000 && cp <= 0xF8FF) || cp == 0xFFFD || cp == 0x2022 || cp == 0x00B7 ||
           cp == 0x25AA || cp == 0x25CF || cp == 0x27A2;
}

bool is_space(char32_t cp) {
    return cp == ' ' || cp == '\t' || cp == '\r' || cp == '\f' || cp == '\v' ||
           cp == 0x00A0 || cp == 0x3000;
}

// Typographic variants that render alike and that OCR cannot tell apart.
char32_t fold_typography(char32_t cp) {
    switch (cp) {
        case 0x201C: case 0x201D: case 0x201E: case 0xFF02: return '"';
        case 0x2018: case 0x2019: case 0x201A: case 0xFF07: return '\'';
        case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2212: return '-';
        case 0x2026: return '.';  // "…" vs "..." still differs in length, which is fine
        case 0xFF1F: return '?';
        case 0xFF0C: return ',';
        case 0xFF1A: return ':';
        case 0xFF08: return '(';
        case 0xFF09: return ')';
        default: return cp;
    }
}

}  // namespace

std::string normalize_slide_text(std::string_view utf8) {
    // Pass 1: map code points into lines.
    std::vector<std::string> lines(1);
    bool pending_space = false;
    for (std::size_t i = 0; i < utf8.size();) {
        const auto cp = utf8::next(utf8, i);
        if (!cp) continue;
        if (*cp == '\n') {
            lines.emplace_back();
            pending_space = false;
        } else if (is_bullet_glyph(*cp)) {
            lines.emplace_back("• ");
            pending_space = false;
        } else if (is_space(*cp)) {
            pending_space = true;
        } else {
            auto& line = lines.back();
            if (pending_space && !line.empty() && line != "• ") line += ' ';
            pending_space = false;
            utf8::append(line, *cp);
        }
    }

    // Pass 2: drop empty lines (a lone bullet counts as empty).
    std::string out;
    for (const auto& line : lines) {
        if (line.empty() || line == "• ") continue;
        if (!out.empty()) out += '\n';
        out += line;
    }
    return out;
}

std::size_t visible_char_count(std::string_view utf8) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < utf8.size();) {
        const auto cp = utf8::next(utf8, i);
        if (cp && *cp != '\n' && !is_space(*cp) && !is_bullet_glyph(*cp)) ++n;
    }
    return n;
}

std::u32string comparable_chars(std::string_view utf8) {
    std::u32string out;
    for (std::size_t i = 0; i < utf8.size();) {
        const auto cp = utf8::next(utf8, i);
        if (!cp || *cp == '\n' || is_space(*cp) || is_bullet_glyph(*cp)) continue;
        out += fold_typography(*cp);
    }
    return out;
}

}  // namespace ll
