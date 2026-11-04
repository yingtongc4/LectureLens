#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace ll {

// Cleans text extracted from a slide's PDF text layer.
// Exported slides encode bullet glyphs as Private Use Area code points
// (e.g. U+F06E from Wingdings) or U+FFFD; these become "\n• " so each bullet
// sits on its own line. Runs of spaces collapse, lines are trimmed and empty
// lines dropped. Invalid UTF-8 bytes are skipped.
std::string normalize_slide_text(std::string_view utf8);

// Number of non-whitespace code points: used to decide whether a page has a
// usable text layer or needs OCR.
std::size_t visible_char_count(std::string_view utf8);

// The visible code points only (no whitespace, no bullets), with typographic
// quotes, dashes and full-width punctuation folded to ASCII: the form in which
// OCR output is compared against the text layer, so layout and typography do
// not count as recognition errors.
std::u32string comparable_chars(std::string_view utf8);

}  // namespace ll
