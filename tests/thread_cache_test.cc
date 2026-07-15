#include <gtest/gtest.h>

#include "../include/thread_cache.hpp"

thread_cache **thread_cache_tls_address_from_a();
thread_cache **thread_cache_tls_address_from_b();

TEST(ThreadCacheTest, RejectsInvalidAllocateSize) {
        thread_cache cache;

        EXPECT_EQ(cache.allocate(0), nullptr);
        EXPECT_EQ(cache.allocate(MAX_BYTES + 1), nullptr);
}

TEST(ThreadCacheTest, IgnoresInvalidDeallocateArguments) {
        thread_cache cache;

        cache.deallocate(nullptr, 64);
        cache.deallocate(reinterpret_cast<void *>(0x1), 0);
        cache.deallocate(reinterpret_cast<void *>(0x1), MAX_BYTES + 1);
}

TEST(ThreadCacheTest, AllocatesAndDeallocatesSmallObject) {
        thread_cache cache;

        void *ptr = cache.allocate(64);
        ASSERT_NE(ptr, nullptr);

        cache.deallocate(ptr, 64);
}

TEST(ThreadCacheTest, AllocatesMultipleObjects) {
        thread_cache cache;

        void *first = cache.allocate(128);
        void *second = cache.allocate(128);
        void *third = cache.allocate(128);

        ASSERT_NE(first, nullptr);
        ASSERT_NE(second, nullptr);
        ASSERT_NE(third, nullptr);
        EXPECT_NE(first, second);
        EXPECT_NE(first, third);
        EXPECT_NE(second, third);

        cache.deallocate(first, 128);
        cache.deallocate(second, 128);
        cache.deallocate(third, 128);
}

TEST(ThreadCacheTest, ThreadLocalPointerIsSharedAcrossTranslationUnits) {
        EXPECT_EQ(thread_cache_tls_address_from_a(), &p_tls_thread_cache);
        EXPECT_EQ(thread_cache_tls_address_from_b(), &p_tls_thread_cache);
        EXPECT_EQ(thread_cache_tls_address_from_a(),
                  thread_cache_tls_address_from_b());
}
