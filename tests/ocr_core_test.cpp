#include <gtest/gtest.h>

#include "core/cer.hpp"
#include "core/text_normalize.hpp"
#include "ocr/ctc.hpp"
#include "ocr/layout.hpp"

namespace ll {
namespace {

TEST(EditDistance, Basics) {
    EXPECT_EQ(edit_distance(U"", U""), 0u);
    EXPECT_EQ(edit_distance(U"abc", U""), 3u);
    EXPECT_EQ(edit_distance(U"kitten", U"sitting"), 3u);
    EXPECT_EQ(edit_distance(U"資訊安全", U"資安全"), 1u);
}

TEST(CharacterErrorRate, RelativeToReference) {
    EXPECT_DOUBLE_EQ(character_error_rate(U"abcd", U"abcd"), 0.0);
    EXPECT_DOUBLE_EQ(character_error_rate(U"abxd", U"abcd"), 0.25);
    EXPECT_DOUBLE_EQ(character_error_rate(U"", U""), 0.0);
    EXPECT_DOUBLE_EQ(character_error_rate(U"x", U""), 1.0);
}

TEST(ComparableChars, IgnoresLayoutAndBullets) {
    EXPECT_EQ(comparable_chars("• Trusted code\n  is sent"), U"Trustedcodeissent");
    EXPECT_EQ(comparable_chars("· Trusted code is sent"), U"Trustedcodeissent");
}

TEST(ComparableChars, FoldsTypography) {
    EXPECT_EQ(comparable_chars("“&” – it’s"), U"\"&\"-it's");
    EXPECT_EQ(comparable_chars("？"), U"?");
}

TEST(LcsLength, ToleratesInsertions) {
    EXPECT_EQ(lcs_length(U"", U"abc"), 0u);
    EXPECT_EQ(lcs_length(U"aXbYc", U"abc"), 3u);
    EXPECT_EQ(lcs_length(U"cba", U"abc"), 1u);
}

TEST(CtcGreedyDecode, MergesRepeatsAndDropsBlanks) {
    const std::vector<std::string> labels = {"<blank>", "a", "b"};
    // argmax per step: a a blank a b b  ->  "aab"
    const float probs[] = {
        0.1f, 0.8f, 0.1f,  //
        0.1f, 0.9f, 0.0f,  //
        0.9f, 0.1f, 0.0f,  //
        0.2f, 0.7f, 0.1f,  //
        0.0f, 0.4f, 0.6f,  //
        0.0f, 0.0f, 1.0f,  //
    };
    const auto d = ocr::ctc_greedy_decode(probs, 6, 3, labels);
    EXPECT_EQ(d.text, "aab");
    EXPECT_NEAR(d.confidence, (0.8f + 0.7f + 0.6f) / 3, 1e-6);
}

TEST(MakeCtcLabels, BlankThenCharsetThenSpace) {
    const auto l = ocr::make_ctc_labels("a\nb\r\n安\n");
    ASSERT_EQ(l.size(), 5u);
    EXPECT_EQ(l[1], "a");
    EXPECT_EQ(l[2], "b");
    EXPECT_EQ(l[3], "安");
    EXPECT_EQ(l[4], " ");
}

TEST(ReadingOrder, RowsTopToBottomThenLeftToRight) {
    // Row 1: "B" (x=200) and "A" (x=10) at similar heights; row 2 below.
    const std::vector<ocr::Box> boxes = {{200, 12, 50, 20}, {10, 10, 50, 20}, {10, 60, 50, 20}};
    const auto rows = ocr::reading_order(boxes);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0], (std::vector<int>{1, 0}));
    EXPECT_EQ(rows[1], (std::vector<int>{2}));
}

TEST(JoinReadingOrder, SpacesWithinRowNewlinesBetween) {
    const std::vector<ocr::OcrLine> lines = {
        {"world", 1.0f, {120, 0, 50, 20}},
        {"hello", 1.0f, {0, 2, 50, 20}},
        {"next", 1.0f, {0, 40, 50, 20}},
    };
    EXPECT_EQ(ocr::join_reading_order(lines), "hello world\nnext");
}

}  // namespace
}  // namespace ll
