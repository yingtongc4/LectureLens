#include "core/chunker.hpp"

#include <gtest/gtest.h>

namespace ll {
namespace {

TEST(ChunkPageText, ShortPageIsOneChunk) {
    const auto c = chunk_page_text("a\nb\nc", 100);
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0], "a\nb\nc");
}

TEST(ChunkPageText, BreaksOnLineBoundaries) {
    const auto c = chunk_page_text("aaaa\nbbbb\ncccc", 9);
    ASSERT_EQ(c.size(), 2u);
    EXPECT_EQ(c[0], "aaaa\nbbbb");
    EXPECT_EQ(c[1], "cccc");
}

TEST(ChunkPageText, LongLineSplitsOnCodePointBoundary) {
    // Each CJK char is 3 bytes; a 4-byte limit must not cut through one.
    const auto c = chunk_page_text("資訊安全", 4);
    ASSERT_EQ(c.size(), 4u);
    for (const auto& s : c) EXPECT_EQ(s.size(), 3u);
}

TEST(ChunkPageText, EmptyInput) { EXPECT_TRUE(chunk_page_text("", 10).empty()); }

}  // namespace
}  // namespace ll
