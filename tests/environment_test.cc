#include <glog/logging.h>
#include <gtest/gtest.h>

TEST(EnvironmentTest, SanityCheck) {
        LOG(INFO) << "EnvironmentTest, SanityCheck";
        EXPECT_EQ(1, 1);
}
