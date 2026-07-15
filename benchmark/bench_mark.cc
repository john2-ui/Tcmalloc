#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <glog/logging.h>
#include <unistd.h>

#include "tcmalloc.hpp"

namespace {

// 使用steady_clock避免系统时间调整影响耗时统计。
using Clock = std::chrono::steady_clock;

/**
 * @brief 抽象分配器接口
 * @note 通过函数指针把malloc/free和tcmalloc/tcfree统一到同一套benchmark流程。
 */
struct Allocator {
        const char *name;
        void *(*alloc)(size_t);
        void (*free)(void *);
};

/**
 * @brief 单个benchmark场景的统计结果
 *
 * rss_before/rss_middle/rss_after用于观察进程常驻内存变化：
 * - before: 场景开始前
 * - middle: 大量分配后或制造碎片后
 * - after: 释放后
 *
 * 注意RSS不是精确碎片率，只是观察内存保留、释放回收趋势的近似指标。
 */
struct BenchResult {
        std::string scenario;
        std::string allocator;
        size_t operations = 0;
        double elapsed_ms = 0.0;
        size_t rss_before = 0;
        size_t rss_middle = 0;
        size_t rss_after = 0;
};

void *malloc_alloc(size_t size) { return std::malloc(size); }

void malloc_free(void *ptr) { std::free(ptr); }

void *tc_alloc(size_t size) { return tcmalloc(size); }

void tc_free(void *ptr) { tcfree(ptr); }

/**
 * @brief 获取当前进程RSS常驻内存大小
 * @return size_t RSS字节数，失败返回0
 *
 * Linux /proc/self/statm 第二列是resident pages，乘以系统页大小得到RSS。
 * benchmark只用它做趋势比较，不用于严格衡量内部碎片。
 */
size_t current_rss_bytes() {
        std::ifstream statm("/proc/self/statm");
        size_t total_pages = 0;
        size_t resident_pages = 0;
        statm >> total_pages >> resident_pages;

        long page_size = sysconf(_SC_PAGESIZE);
        if (page_size <= 0) {
                return 0;
        }

        return resident_pages * static_cast<size_t>(page_size);
}

/**
 * @brief 写入分配到的内存首尾字节
 * @note 确保操作系统真正提交物理页，避免只分配虚拟地址导致RSS统计失真。
 */
void touch_memory(void *ptr, size_t size) {
        if (ptr == nullptr || size == 0) {
                return;
        }

        auto *bytes = static_cast<unsigned char *>(ptr);
        bytes[0] = static_cast<unsigned char>(size);
        bytes[size - 1] = static_cast<unsigned char>(size >> 8);
}

/**
 * @brief 计算两个时间点之间的毫秒数
 */
double elapsed_ms(Clock::time_point begin, Clock::time_point end) {
        return std::chrono::duration<double, std::milli>(end - begin).count();
}

/**
 * @brief 固定大小分配/释放测试
 * @note 用于观察单一size class下分配器的缓存命中和批量分配效率。
 */
BenchResult fixed_size_benchmark(const Allocator &allocator, size_t iterations,
                                 size_t size) {
        BenchResult result;
        result.scenario = "fixed-" + std::to_string(size);
        result.allocator = allocator.name;
        result.operations = iterations * 2;
        result.rss_before = current_rss_bytes();

        // 先批量分配、再批量释放，模拟对象生命周期相对集中的场景。
        std::vector<void *> ptrs(iterations, nullptr);
        auto begin = Clock::now();
        for (size_t i = 0; i < iterations; ++i) {
                ptrs[i] = allocator.alloc(size);
                touch_memory(ptrs[i], size);
        }
        result.rss_middle = current_rss_bytes();

        for (void *ptr : ptrs) {
                allocator.free(ptr);
        }
        auto end = Clock::now();

        result.rss_after = current_rss_bytes();
        result.elapsed_ms = elapsed_ms(begin, end);
        return result;
}

/**
 * @brief 混合大小分配/释放测试
 * @note 覆盖多个size class，随机释放顺序会比固定大小更接近一般业务负载。
 */
BenchResult mixed_size_benchmark(const Allocator &allocator,
                                 size_t iterations) {
        static const size_t kSizes[] = {8,    16,   32,        64,
                                        128,  256,  512,       1024,
                                        4096, 8192, 16 * 1024, 64 * 1024};

        BenchResult result;
        result.scenario = "mixed-small-large";
        result.allocator = allocator.name;
        result.operations = iterations * 2;
        result.rss_before = current_rss_bytes();

        std::vector<void *> ptrs(iterations, nullptr);
        // 固定随机种子保证每次运行的请求序列一致，便于横向比较。
        std::mt19937 rng(20260715);
        std::uniform_int_distribution<size_t> dist(0, std::size(kSizes) - 1);

        auto begin = Clock::now();
        for (size_t i = 0; i < iterations; ++i) {
                size_t size = kSizes[dist(rng)];
                ptrs[i] = allocator.alloc(size);
                touch_memory(ptrs[i], size);
        }
        result.rss_middle = current_rss_bytes();

        // 打乱释放顺序，避免严格LIFO释放过于有利于缓存。
        std::shuffle(ptrs.begin(), ptrs.end(), rng);
        for (void *ptr : ptrs) {
                allocator.free(ptr);
        }
        auto end = Clock::now();

        result.rss_after = current_rss_bytes();
        result.elapsed_ms = elapsed_ms(begin, end);
        return result;
}

/**
 * @brief 碎片趋势测试
 *
 * 流程：
 * 1. 分配一批不同大小的对象。
 * 2. 释放偶数位置对象，制造空洞。
 * 3. 再用不同大小重新填充这些空洞。
 * 4. 最后全部释放。
 *
 * rss_middle和rss_after可以粗略观察分配器是否保留了较多页、是否容易形成碎片。
 */
BenchResult fragmentation_benchmark(const Allocator &allocator,
                                    size_t iterations) {
        static const size_t kSizes[] = {64,   128,       512,
                                        4096, 16 * 1024, MAX_BYTES + 8192};

        BenchResult result;
        result.scenario = "fragmentation-proxy";
        result.allocator = allocator.name;
        result.operations = iterations * 3;
        result.rss_before = current_rss_bytes();

        std::vector<void *> ptrs(iterations, nullptr);
        std::vector<size_t> sizes(iterations, 0);

        auto begin = Clock::now();
        for (size_t i = 0; i < iterations; ++i) {
                sizes[i] = kSizes[i % std::size(kSizes)];
                ptrs[i] = allocator.alloc(sizes[i]);
                touch_memory(ptrs[i], sizes[i]);
        }

        // 释放一半对象，制造非连续空闲区间。
        for (size_t i = 0; i < iterations; i += 2) {
                allocator.free(ptrs[i]);
                ptrs[i] = nullptr;
        }
        result.rss_middle = current_rss_bytes();

        // 用错位的大小重新分配，制造“旧洞大小”和“新请求大小”不匹配的情况。
        for (size_t i = 0; i < iterations; i += 2) {
                sizes[i] = kSizes[(i + 3) % std::size(kSizes)];
                ptrs[i] = allocator.alloc(sizes[i]);
                touch_memory(ptrs[i], sizes[i]);
        }

        for (void *ptr : ptrs) {
                allocator.free(ptr);
        }
        auto end = Clock::now();

        result.rss_after = current_rss_bytes();
        result.elapsed_ms = elapsed_ms(begin, end);
        return result;
}

/**
 * @brief 多线程分配/释放测试
 * @note 主要观察thread cache是否减少锁竞争，以及malloc在多线程下的表现。
 */
BenchResult threaded_benchmark(const Allocator &allocator, size_t iterations) {
        size_t thread_count = std::max<size_t>(
            2, std::min<size_t>(8, std::thread::hardware_concurrency()));
        size_t per_thread = iterations / thread_count;
        if (per_thread == 0) {
                per_thread = 1;
        }

        BenchResult result;
        result.scenario = "threaded-" + std::to_string(thread_count);
        result.allocator = allocator.name;
        result.operations = per_thread * thread_count * 2;
        result.rss_before = current_rss_bytes();

        auto begin = Clock::now();
        std::vector<std::thread> threads;
        threads.reserve(thread_count);
        for (size_t t = 0; t < thread_count; ++t) {
                threads.emplace_back([&, t]() {
                        for (size_t i = 0; i < per_thread; ++i) {
                                // 每个线程使用略有差异的小对象大小，避免所有请求落在同一个桶。
                                size_t size = 16 + ((i + t) % 1024);
                                void *ptr = allocator.alloc(size);
                                touch_memory(ptr, size);
                                allocator.free(ptr);
                        }
                });
        }

        for (auto &thread : threads) {
                thread.join();
        }
        auto end = Clock::now();

        result.rss_middle = current_rss_bytes();
        result.rss_after = result.rss_middle;
        result.elapsed_ms = elapsed_ms(begin, end);
        return result;
}

/**
 * @brief 以CSV格式输出单个场景结果
 */
void print_result(std::ostream &out, const BenchResult &result) {
        double seconds = result.elapsed_ms / 1000.0;
        double ops_per_sec = seconds > 0.0 ? result.operations / seconds : 0.0;

        out << result.scenario << ',' << result.allocator << ','
            << result.operations << ',' << result.elapsed_ms << ','
            << ops_per_sec << ',' << result.rss_before << ','
            << result.rss_middle << ',' << result.rss_after << '\n';
        out.flush();
}

/**
 * @brief 对指定分配器运行全部benchmark场景
 */
void run_allocator(std::ostream &out, const Allocator &allocator,
                   size_t iterations) {
        print_result(out, fixed_size_benchmark(allocator, iterations, 64));
        print_result(out, fixed_size_benchmark(allocator, iterations, 1024));
        print_result(out, mixed_size_benchmark(allocator, iterations));
        print_result(out, fragmentation_benchmark(allocator, iterations / 2));
        print_result(out, threaded_benchmark(allocator, iterations));
}

/**
 * @brief 解析形如--key=value的命令行参数
 */
bool has_arg_value(const char *arg, const char *prefix, std::string &value) {
        std::string text(arg);
        std::string key(prefix);
        if (text.rfind(key, 0) != 0) {
                return false;
        }

        value = text.substr(key.size());
        return true;
}

} // namespace

int main(int argc, char **argv) {
        google::InitGoogleLogging(argv[0]);
        google::InstallFailureSignalHandler();
        FLAGS_logtostderr = true;
        // benchmark关注分配器性能，默认关闭INFO日志避免日志IO干扰结果。
        FLAGS_minloglevel = 2;

        size_t iterations = 20000;
        std::string allocator_name = "all";
        std::string output_path = "benchmark/benchmark_result.csv";

        // 支持：
        //   --iterations=N
        //   --allocator=malloc|tcmalloc|all
        //   --output=benchmark/result.csv
        for (int i = 1; i < argc; ++i) {
                std::string value;
                if (has_arg_value(argv[i], "--iterations=", value)) {
                        iterations = std::stoull(value);
                } else if (has_arg_value(argv[i], "--allocator=", value)) {
                        allocator_name = value;
                } else if (has_arg_value(argv[i], "--output=", value)) {
                        output_path = value;
                }
        }

        Allocator malloc_allocator{"malloc", malloc_alloc, malloc_free};
        Allocator tc_allocator{"tcmalloc", tc_alloc, tc_free};

        std::ofstream output(output_path);
        if (!output.is_open()) {
                std::cerr << "failed to open output file: " << output_path
                          << '\n';
                return 1;
        }

        // 输出CSV，方便用脚本或表格工具分析。
        output << "scenario,allocator,operations,elapsed_ms,ops_per_sec,"
                  "rss_before,rss_middle,rss_after\n";
        output.flush();

        if (allocator_name == "malloc") {
                run_allocator(output, malloc_allocator, iterations);
        } else if (allocator_name == "tcmalloc") {
                run_allocator(output, tc_allocator, iterations);
        } else {
                run_allocator(output, malloc_allocator, iterations);
                run_allocator(output, tc_allocator, iterations);
        }

        std::cout << "benchmark result saved to: " << output_path << '\n';

        return 0;
}
