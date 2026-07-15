#include <gtest/gtest.h>

#include "../include/tcmalloc.hpp"

TEST(TcmallocTest, RejectsZeroSize) { EXPECT_EQ(tcmalloc(0), nullptr); }

TEST(TcmallocTest, IgnoresNullFree) { tcfree(nullptr); }

TEST(TcmallocTest, AllocatesAndFreesSmallObject) {
        void *ptr = tcmalloc(64);
        ASSERT_NE(ptr, nullptr);

        tcfree(ptr);
}

TEST(TcmallocTest, AllocatesDistinctSmallObjects) {
        void *first = tcmalloc(128);
        void *second = tcmalloc(128);

        ASSERT_NE(first, nullptr);
        ASSERT_NE(second, nullptr);
        EXPECT_NE(first, second);

        tcfree(first);
        tcfree(second);
}

TEST(TcmallocTest, AllocatesAndFreesLargeObject) {
        void *ptr = tcmalloc(MAX_BYTES + 1);
        ASSERT_NE(ptr, nullptr);

        tcfree(ptr);
}
