#include <gtest/gtest.h>

#include "../include/comm.hpp"

TEST(SizeClassTest, RoundsUpBySizeRange) {
        EXPECT_EQ(size_class::round_up(0), 0);
        EXPECT_EQ(size_class::round_up(1), 8);
        EXPECT_EQ(size_class::round_up(128), 128);
        EXPECT_EQ(size_class::round_up(129), 144);
        EXPECT_EQ(size_class::round_up(1025), 1152);
        EXPECT_EQ(size_class::round_up((8 * 1024) + 1), 9 * 1024);
        EXPECT_EQ(size_class::round_up((64 * 1024) + 1), 72 * 1024);
}

TEST(SizeClassTest, CalculatesBucketIndexBoundaries) {
        EXPECT_EQ(size_class::bucket_index(0), BUCKETS_NUM);
        EXPECT_EQ(size_class::bucket_index(1), 0);
        EXPECT_EQ(size_class::bucket_index(8), 0);
        EXPECT_EQ(size_class::bucket_index(128), 15);
        EXPECT_EQ(size_class::bucket_index(129), 16);
        EXPECT_EQ(size_class::bucket_index(1024), 71);
        EXPECT_EQ(size_class::bucket_index(1025), 72);
        EXPECT_EQ(size_class::bucket_index(8 * 1024), 127);
        EXPECT_EQ(size_class::bucket_index((8 * 1024) + 1), 128);
        EXPECT_EQ(size_class::bucket_index(64 * 1024), 183);
        EXPECT_EQ(size_class::bucket_index((64 * 1024) + 1), 184);
        EXPECT_EQ(size_class::bucket_index(MAX_BYTES), BUCKETS_NUM - 1);
        EXPECT_EQ(size_class::bucket_index(MAX_BYTES + 1), BUCKETS_NUM);
}

TEST(SizeClassTest, CalculatesMoveSizeAndPages) {
        EXPECT_EQ(size_class::num_move_size(0), 0);
        EXPECT_EQ(size_class::num_move_size(1), 512);
        EXPECT_EQ(size_class::num_move_size(MAX_BYTES), 2);

        EXPECT_EQ(size_class::num_move_page(0), 0);
        EXPECT_EQ(size_class::num_move_page(1), 1);
        EXPECT_EQ(size_class::num_move_page(MAX_BYTES), 64);
}
