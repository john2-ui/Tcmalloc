#include <gtest/gtest.h>

#include "../include/comm.hpp"

TEST(SpanTest, InitializesMembersSafely) {
        span item;

        EXPECT_EQ(item.page_id_, 0);
        EXPECT_EQ(item.page_num_, 0);
        EXPECT_EQ(item.obj_size_, 0);
        EXPECT_EQ(item.use_count_, 0);
        EXPECT_EQ(item.free_list_, nullptr);
        EXPECT_FALSE(item.is_use_);
        EXPECT_EQ(item.next_, nullptr);
        EXPECT_EQ(item.prev_, nullptr);
}

TEST(SpanListTest, HandlesEmptyListSafely) {
        span_list list;

        EXPECT_TRUE(list.empty());
        EXPECT_EQ(list.begin(), list.end());
        EXPECT_EQ(list.pop_front(), nullptr);

        list.insert(nullptr, nullptr);
        list.erase(nullptr);
        list.erase(list.end());
        EXPECT_TRUE(list.empty());
}

TEST(SpanListTest, PushesAndPopsFront) {
        span_list list;
        span first;
        span second;

        list.push_front(&first);
        EXPECT_FALSE(list.empty());
        EXPECT_EQ(list.begin(), &first);

        list.push_front(&second);
        EXPECT_EQ(list.begin(), &second);
        EXPECT_EQ(list.pop_front(), &second);
        EXPECT_EQ(list.pop_front(), &first);
        EXPECT_TRUE(list.empty());
}

TEST(SpanListTest, InsertsBeforePositionAndErases) {
        span_list list;
        span first;
        span second;
        span inserted;

        list.push_front(&second);
        list.push_front(&first);
        list.insert(&second, &inserted);

        EXPECT_EQ(list.begin(), &first);
        EXPECT_EQ(first.next_, &inserted);
        EXPECT_EQ(inserted.prev_, &first);
        EXPECT_EQ(inserted.next_, &second);
        EXPECT_EQ(second.prev_, &inserted);

        list.erase(&inserted);
        EXPECT_EQ(first.next_, &second);
        EXPECT_EQ(second.prev_, &first);
        EXPECT_EQ(inserted.next_, nullptr);
        EXPECT_EQ(inserted.prev_, nullptr);
}
