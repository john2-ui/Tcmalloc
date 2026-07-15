#include <gtest/gtest.h>

#include "../include/object_pool.hpp"

namespace {

struct PoolObject {
        explicit PoolObject(int value = 0) : value(value) { constructed++; }

        ~PoolObject() { destructed++; }

        int value = 0;

        static int constructed;
        static int destructed;
};

int PoolObject::constructed = 0;
int PoolObject::destructed = 0;

void reset_pool_object_counter() {
        PoolObject::constructed = 0;
        PoolObject::destructed = 0;
}

} // namespace

TEST(ObjectPoolTest, ConstructsObjectWithArguments) {
        reset_pool_object_counter();
        object_pool<PoolObject> pool;

        PoolObject *obj = pool.new_(42);
        ASSERT_NE(obj, nullptr);
        EXPECT_EQ(obj->value, 42);
        EXPECT_EQ(PoolObject::constructed, 1);

        pool.delete_(obj);
        EXPECT_EQ(PoolObject::destructed, 1);
}

TEST(ObjectPoolTest, ReusesDeletedObjectStorage) {
        reset_pool_object_counter();
        object_pool<PoolObject> pool;

        PoolObject *first = pool.new_(1);
        ASSERT_NE(first, nullptr);
        pool.delete_(first);

        PoolObject *second = pool.new_(2);
        ASSERT_NE(second, nullptr);
        EXPECT_EQ(second, first);
        EXPECT_EQ(second->value, 2);
        EXPECT_EQ(PoolObject::constructed, 2);

        pool.delete_(second);
        EXPECT_EQ(PoolObject::destructed, 2);
}

TEST(ObjectPoolTest, IgnoresNullDelete) {
        reset_pool_object_counter();
        object_pool<PoolObject> pool;

        pool.delete_(nullptr);
        EXPECT_EQ(PoolObject::constructed, 0);
        EXPECT_EQ(PoolObject::destructed, 0);
}
