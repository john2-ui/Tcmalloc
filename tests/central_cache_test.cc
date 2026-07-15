#include <gtest/gtest.h>

#include "../include/central_cache.hpp"

TEST(CentralCacheTest, RejectsInvalidFetchArguments) {
        central_cache &cache = central_cache::get_instance();
        void *start = reinterpret_cast<void *>(0x1);
        void *end = reinterpret_cast<void *>(0x2);

        EXPECT_EQ(cache.fetch_range_obj(start, end, 0, 64), 0);
        EXPECT_EQ(start, nullptr);
        EXPECT_EQ(end, nullptr);

        EXPECT_EQ(cache.fetch_range_obj(start, end, 1, 0), 0);
        EXPECT_EQ(start, nullptr);
        EXPECT_EQ(end, nullptr);

        EXPECT_EQ(cache.fetch_range_obj(start, end, 1, MAX_BYTES + 1), 0);
        EXPECT_EQ(start, nullptr);
        EXPECT_EQ(end, nullptr);
}

TEST(CentralCacheTest, FetchesObjectsFromCentralCache) {
        central_cache &cache = central_cache::get_instance();
        void *start = nullptr;
        void *end = nullptr;

        size_t actual = cache.fetch_range_obj(start, end, 4, 64);
        ASSERT_GT(actual, 0);
        ASSERT_NE(start, nullptr);
        ASSERT_NE(end, nullptr);
        EXPECT_EQ(next_obj(end), nullptr);

        cache.release_list_to_span(start, 64);
}

TEST(CentralCacheTest, ReleasesAndFetchesAgain) {
        central_cache &cache = central_cache::get_instance();
        void *start = nullptr;
        void *end = nullptr;

        size_t first_count = cache.fetch_range_obj(start, end, 2, 128);
        ASSERT_GT(first_count, 0);
        ASSERT_NE(start, nullptr);
        cache.release_list_to_span(start, 128);

        start = nullptr;
        end = nullptr;
        size_t second_count = cache.fetch_range_obj(start, end, 2, 128);
        ASSERT_GT(second_count, 0);
        ASSERT_NE(start, nullptr);
        cache.release_list_to_span(start, 128);
}

TEST(CentralCacheTest, IgnoresInvalidReleaseArguments) {
        central_cache &cache = central_cache::get_instance();

        cache.release_list_to_span(nullptr, 64);
        cache.release_list_to_span(reinterpret_cast<void *>(0x1), 0);
        cache.release_list_to_span(reinterpret_cast<void *>(0x1),
                                   MAX_BYTES + 1);
}
