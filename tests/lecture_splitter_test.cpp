#include "core/lecture_splitter.hpp"

#include <gtest/gtest.h>

namespace ll {
namespace {

const std::string kHeader = R"(CS915/(CS)?435 Advanced Computer Security)";

TEST(SplitLectures, TitleAfterDashOnHeaderLine) {
    const std::vector<std::string> pages = {
        "CS915/435 Advanced Computer Security - Software security (I)\nBuffer Overflow",
        "Outline",
        "CS915/435 Advanced Computer Security - Web security (II)\nSQL Injection",
        "Prepared statements",
    };
    const auto lec = split_lectures(pages, kHeader, "deck");
    ASSERT_EQ(lec.size(), 2u);
    EXPECT_EQ(lec[0].title, "Software security (I)");
    EXPECT_EQ(lec[0].first_page, 1);
    EXPECT_EQ(lec[0].last_page, 2);
    EXPECT_EQ(lec[1].title, "Web security (II)");
    EXPECT_EQ(lec[1].first_page, 3);
    EXPECT_EQ(lec[1].last_page, 4);
}

TEST(SplitLectures, TitleOnNextLineWithLeadingDash) {
    const std::vector<std::string> pages = {
        "CS915/CS435 Advanced Computer Security\n- Block cipher I\nFeng Hao",
        "AES",
    };
    const auto lec = split_lectures(pages, kHeader, "deck");
    ASSERT_EQ(lec.size(), 1u);
    EXPECT_EQ(lec[0].title, "Block cipher I");
    EXPECT_EQ(lec[0].last_page, 2);
}

TEST(SplitLectures, HeaderWrappedOverTwoLines) {
    const std::vector<std::string> pages = {
        "CS915/435 Advanced Computer Security\n- Users and Security\nFeng Hao",
        "CS915/435 Advanced Computer\nSecurity\nClassical Cryptography\nFeng Hao",
        "Caesar cipher",
        "CS915/CS435 Advanced Computer\nSecurity\n- Hash\nFeng Hao",
    };
    const auto lec = split_lectures(pages, kHeader, "deck");
    ASSERT_EQ(lec.size(), 3u);
    EXPECT_EQ(lec[0].title, "Users and Security");
    EXPECT_EQ(lec[1].title, "Classical Cryptography");
    EXPECT_EQ(lec[1].first_page, 2);
    EXPECT_EQ(lec[1].last_page, 3);
    EXPECT_EQ(lec[2].title, "Hash");
}

TEST(SplitLectures, PagesBeforeFirstTitleGetFallback) {
    const std::vector<std::string> pages = {
        "Cover",
        "CS915/435 Advanced Computer Security - Hash",
    };
    const auto lec = split_lectures(pages, kHeader, "deck");
    ASSERT_EQ(lec.size(), 2u);
    EXPECT_EQ(lec[0].title, "deck");
    EXPECT_EQ(lec[0].last_page, 1);
    EXPECT_EQ(lec[1].title, "Hash");
    EXPECT_EQ(lec[1].seq, 1);
}

TEST(SplitLectures, NoTitlePagesIsOneLecture) {
    const auto lec = split_lectures({"a", "b", "c"}, kHeader, "deck");
    ASSERT_EQ(lec.size(), 1u);
    EXPECT_EQ(lec[0].first_page, 1);
    EXPECT_EQ(lec[0].last_page, 3);
}

TEST(SplitLectures, HeaderBelowSearchWindowIsIgnored) {
    const std::vector<std::string> pages = {
        "CS915/435 Advanced Computer Security - Intro",
        "line1\nline2\nline3\nsee CS915/435 Advanced Computer Security - not a title",
    };
    EXPECT_EQ(split_lectures(pages, kHeader, "deck").size(), 1u);
}

}  // namespace
}  // namespace ll
