#include <cmath>

#include <gtest/gtest.h>

#include "core/rrf.hpp"
#include "embed/pooling.hpp"

namespace ll {
namespace {

TEST(ReciprocalRankFusion, AgreementBeatsSingleListTopRank) {
    // 2 is ranked 2nd by both lists; 1 and 3 are each 1st in only one list.
    const auto f = reciprocal_rank_fusion({1, 2}, {3, 2}, 60);
    ASSERT_EQ(f.size(), 3u);
    EXPECT_EQ(f[0].id, 2);
    EXPECT_EQ(f[0].rank_a, 2);
    EXPECT_EQ(f[0].rank_b, 2);
    EXPECT_NEAR(f[0].score, 2.0 / 62, 1e-12);
    EXPECT_EQ(f[1].id, 1);  // tie with 3 broken by smaller id
    EXPECT_EQ(f[2].id, 3);
    EXPECT_EQ(f[2].rank_a, 0);
}

TEST(ReciprocalRankFusion, OneListEmpty) {
    const auto f = reciprocal_rank_fusion({}, {7, 8});
    ASSERT_EQ(f.size(), 2u);
    EXPECT_EQ(f[0].id, 7);
    EXPECT_EQ(f[0].rank_a, 0);
    EXPECT_EQ(f[0].rank_b, 1);
}

TEST(ReciprocalRankFusion, DuplicateKeepsBestRank) {
    const auto f = reciprocal_rank_fusion({5, 5}, {});
    ASSERT_EQ(f.size(), 1u);
    EXPECT_EQ(f[0].rank_a, 1);
}

TEST(MeanPoolNormalize, IgnoresMaskedTokensAndNormalizes) {
    // batch 1, 3 tokens, dim 2; the third token is padding.
    const float hidden[] = {1, 0, 3, 0, 100, 100};
    const auto v = embed::mean_pool_normalize(hidden, 1, 3, 2, {1, 1, 0});
    ASSERT_EQ(v.size(), 1u);
    EXPECT_FLOAT_EQ(v[0][0], 1.0f);  // mean (2, 0) normalized
    EXPECT_FLOAT_EQ(v[0][1], 0.0f);
}

TEST(MeanPoolNormalize, UnitLength) {
    const float hidden[] = {3, 4};
    const auto v = embed::mean_pool_normalize(hidden, 1, 1, 2, {1});
    EXPECT_NEAR(std::sqrt(embed::dot(v[0], v[0])), 1.0, 1e-6);
    EXPECT_NEAR(v[0][0], 0.6, 1e-6);
}

}  // namespace
}  // namespace ll
