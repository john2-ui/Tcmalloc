#include <gtest/gtest.h>

#include "../include/page_cache.hpp"

TEST(PageCacheTest, HandlesInvalidInputsSafely) {
        page_cache &cache = page_cache::get_instance();

        EXPECT_EQ(cache.new_span(0), nullptr);
        EXPECT_EQ(cache.map_obj_to_span(nullptr), nullptr);
        cache.release_span_to_page(nullptr);
}

TEST(PageCacheTest, AllocatesSmallSpanAndMapsObjectAddress) {
        page_cache &cache = page_cache::get_instance();

        span *s = cache.new_span(1);
        ASSERT_NE(s, nullptr);
        EXPECT_EQ(s->page_num_, 1);
        EXPECT_TRUE(s->is_use_);

        void *obj = reinterpret_cast<void *>(s->page_id_ << PAGE_SHIFT);
        EXPECT_EQ(cache.map_obj_to_span(obj), s);

        cache.release_span_to_page(s);
        EXPECT_FALSE(s->is_use_);
}

TEST(PageCacheTest, AllocatesLargeSpanAndMapsObjectAddress) {
        page_cache &cache = page_cache::get_instance();

        span *s = cache.new_span(PAGES_NUM);
        ASSERT_NE(s, nullptr);
        EXPECT_EQ(s->page_num_, PAGES_NUM);
        EXPECT_TRUE(s->is_use_);

        void *obj = reinterpret_cast<void *>(s->page_id_ << PAGE_SHIFT);
        EXPECT_EQ(cache.map_obj_to_span(obj), s);

        cache.release_span_to_page(s);
}
