#include <gtest/gtest.h>

#include "../include/thread_cache.hpp"

thread_cache **thread_cache_tls_address_from_a();
thread_cache **thread_cache_tls_address_from_b();

TEST(ThreadCacheTest, ThreadLocalPointerIsSharedAcrossTranslationUnits) {
        EXPECT_EQ(thread_cache_tls_address_from_a(), &p_tls_thread_cache);
        EXPECT_EQ(thread_cache_tls_address_from_b(), &p_tls_thread_cache);
        EXPECT_EQ(thread_cache_tls_address_from_a(),
                  thread_cache_tls_address_from_b());
}
