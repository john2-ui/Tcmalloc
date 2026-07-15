#include <gtest/gtest.h>

#include "../include/page_map.hpp"

TEST(PageMapTest, ReturnsNullForMissingMapping) {
        TCMalloc_PageMap<10> page_map;

        EXPECT_EQ(page_map.get(1), nullptr);

        span item;
        page_map.set(1, &item);
        EXPECT_EQ(page_map.get(1), nullptr);
}

TEST(PageMapTest, EnsuresRangeAndStoresMappings) {
        TCMalloc_PageMap<10> page_map;
        span first;
        span second;

        ASSERT_TRUE(page_map.Ensure(3, 2));
        page_map.set(3, &first);
        page_map.set(4, &second);

        EXPECT_EQ(page_map.get(3), &first);
        EXPECT_EQ(page_map.get(4), &second);
        EXPECT_EQ(page_map.get(5), nullptr);
}

TEST(PageMapTest, HandlesLastValidKey) {
        TCMalloc_PageMap<10> page_map;
        span item;

        ASSERT_TRUE(page_map.Ensure(1023, 1));
        page_map.set(1023, &item);

        EXPECT_EQ(page_map.get(1023), &item);
}

TEST(PageMapTest, RejectsInvalidRangesAndKeys) {
        TCMalloc_PageMap<10> page_map;

        EXPECT_TRUE(page_map.Ensure(0, 0));
        EXPECT_FALSE(page_map.Ensure(1024, 1));
        EXPECT_FALSE(page_map.Ensure(1023, 2));
        EXPECT_EQ(page_map.get(1024), nullptr);

        span item;
        page_map.set(1024, &item);
        EXPECT_EQ(page_map.get(1024), nullptr);
}
