#include "core/text_normalize.hpp"

#include <gtest/gtest.h>

namespace ll {
namespace {

TEST(NormalizeSlideText, PrivateUseBulletsBecomeLines) {
    // U+F06E (Wingdings bullet) as exported from PowerPoint.
    const std::string in = "Outline\n\xEF\x81\xAE Buffer Overflow Attack \xEF\x81\xAE Countermeasures";
    EXPECT_EQ(normalize_slide_text(in), "Outline\n• Buffer Overflow Attack\n• Countermeasures");
}

TEST(NormalizeSlideText, ReplacementCharIsTreatedAsBullet) {
    const std::string in = "\xEF\xBF\xBD Trusted code \xEF\xBF\xBD Untrusted data";
    EXPECT_EQ(normalize_slide_text(in), "• Trusted code\n• Untrusted data");
}

TEST(NormalizeSlideText, CollapsesWhitespaceAndDropsEmptyLines) {
    EXPECT_EQ(normalize_slide_text("  a   b\t c \n\n\n  d  \f"), "a b c\nd");
}

TEST(NormalizeSlideText, KeepsCjkAndSkipsInvalidBytes) {
    EXPECT_EQ(normalize_slide_text("資訊\xFF安全"), "資訊安全");
}

TEST(NormalizeSlideText, LoneBulletIsDropped) {
    EXPECT_EQ(normalize_slide_text("Title\n\xEF\x81\xAE   \n"), "Title");
}

TEST(VisibleCharCount, CountsCodePointsNotBytes) {
    EXPECT_EQ(visible_char_count("ab c"), 3u);
    EXPECT_EQ(visible_char_count("資訊 安全"), 4u);
    EXPECT_EQ(visible_char_count("• x"), 1u);
}

}  // namespace
}  // namespace ll
