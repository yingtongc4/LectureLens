#include "core/lecture_splitter.hpp"

#include <optional>
#include <regex>
#include <sstream>

namespace ll {
namespace {

constexpr std::size_t kHeaderSearchLines = 3;
constexpr std::size_t kMaxHeaderLines = 2;

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\f");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\f");
    return s.substr(b, e - b + 1);
}

std::vector<std::string> first_lines(const std::string& text, std::size_t n) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;
    while (out.size() < n && std::getline(in, line)) {
        line = trim(line);
        if (!line.empty()) out.push_back(line);
    }
    return out;
}

std::string strip_dash(std::string s) {
    s = trim(s);
    while (!s.empty() && (s.front() == '-' || s.front() == ' ')) s.erase(0, 1);
    return s;
}

std::optional<std::string> title_of(const std::string& page_text, const std::regex& header) {
    const auto lines = first_lines(page_text, kHeaderSearchLines + kMaxHeaderLines);
    for (std::size_t i = 0; i < lines.size() && i < kHeaderSearchLines; ++i) {
        // A long header may wrap, e.g. "...Advanced Computer" / "Security".
        std::string joined;
        for (std::size_t span = 1; span <= kMaxHeaderLines && i + span <= lines.size(); ++span) {
            if (!joined.empty()) joined += ' ';
            joined += lines[i + span - 1];
            // The header must begin on line i; a later span would otherwise
            // glue body text from line i onto a match in line i + 1.
            std::smatch m;
            if (!std::regex_search(joined, m, header) ||
                static_cast<std::size_t>(m.position(0)) >= lines[i].size()) {
                continue;
            }
            auto rest = strip_dash(m.suffix().str());
            if (!rest.empty()) return rest;
            const auto next = i + span;
            if (next < lines.size()) return strip_dash(lines[next]);
            return std::string{};
        }
    }
    return std::nullopt;
}

}  // namespace

std::vector<LectureSpan> split_lectures(const std::vector<std::string>& page_texts,
                                        const std::string& header_regex,
                                        const std::string& fallback_title) {
    std::vector<LectureSpan> out;
    if (page_texts.empty()) return out;

    const std::regex header(header_regex, std::regex::ECMAScript | std::regex::icase);
    const int n = static_cast<int>(page_texts.size());

    for (int p = 1; p <= n; ++p) {
        const auto title = title_of(page_texts[p - 1], header);
        if (title) {
            if (!out.empty()) out.back().last_page = p - 1;
            out.push_back({static_cast<int>(out.size()),
                           title->empty() ? fallback_title : *title, p, n});
        } else if (out.empty()) {
            out.push_back({0, fallback_title, p, n});
        }
    }
    return out;
}

}  // namespace ll
