#include <glog/logging.h>
#include <gtest/gtest.h>

TEST(EnvironmentTest, SanityCheck) {
    LOG(INFO) << "EnvironmentTest, SanityCheck";
    EXPECT_EQ(1, 1);
}

int main(int argc, char** argv) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = true;

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}