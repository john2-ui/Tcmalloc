#include <gtest/gtest.h>

#include "../include/comm.hpp"

TEST(SystemAllocTest, AllocatesOnePage) {
        void *ptr = system_alloc(1);
        ASSERT_NE(ptr, nullptr);
        system_free(ptr, 1 << PAGE_SHIFT);
}
