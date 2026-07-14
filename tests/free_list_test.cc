#include <gtest/gtest.h>

#include "../include/comm.hpp"

TEST(FreeListTest, IgnoresInvalidPushAndPop) {
        free_list list;

        list.push(nullptr);
        EXPECT_TRUE(list.empty());
        EXPECT_EQ(list.size(), 0);
        EXPECT_EQ(list.pop(), nullptr);

        void *start = nullptr;
        void *end = nullptr;
        list.pop(start, end, 1);
        EXPECT_EQ(start, nullptr);
        EXPECT_EQ(end, nullptr);
}

TEST(FreeListTest, PushAndPopSingleObject) {
        free_list list;
        void *obj = system_alloc(1);
        ASSERT_NE(obj, nullptr);

        list.push(obj);
        EXPECT_FALSE(list.empty());
        EXPECT_EQ(list.size(), 1);
        EXPECT_EQ(list.pop(), obj);
        EXPECT_TRUE(list.empty());

        system_free(obj, 1 << PAGE_SHIFT);
}

TEST(FreeListTest, PushAndPopObjectRange) {
        free_list list;
        void *first = system_alloc(1);
        void *second = system_alloc(1);
        ASSERT_NE(first, nullptr);
        ASSERT_NE(second, nullptr);

        next_obj(first) = second;
        next_obj(second) = nullptr;
        list.push(first, second, 2);

        void *start = nullptr;
        void *end = nullptr;
        list.pop(start, end, 2);
        EXPECT_EQ(start, first);
        EXPECT_EQ(end, second);
        EXPECT_TRUE(list.empty());

        system_free(first, 1 << PAGE_SHIFT);
        system_free(second, 1 << PAGE_SHIFT);
}
