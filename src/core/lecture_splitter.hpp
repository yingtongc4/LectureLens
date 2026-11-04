#pragma once

#include <string>
#include <vector>

namespace ll {

struct LectureSpan {
    int seq = 0;          // 0-based order within the document
    std::string title;
    int first_page = 1;   // 1-based, inclusive
    int last_page = 1;    // 1-based, inclusive
};

// Splits a merged slide deck into lectures by detecting title pages.
// A page is a title page when one of its first few non-empty lines matches
// `header_regex` (ECMAScript). The title is the text after " - " on that
// line, or else the next non-empty line with any leading "- " removed.
// Pages before the first title page form a lecture named `fallback_title`;
// a document with no title page is a single lecture with that name.
std::vector<LectureSpan> split_lectures(const std::vector<std::string>& page_texts,
                                        const std::string& header_regex,
                                        const std::string& fallback_title);

}  // namespace ll
