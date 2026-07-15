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

using Clock = std::chrono::steady_clock;

struct Allocator {
        const char *name;
        void *(*alloc)(size_t);
        void (*free)(void *);
};

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

void touch_memory(void *ptr, size_t size) {
        if (ptr == nullptr || size == 0) {
                return;
        }

        auto *bytes = static_cast<unsigned char *>(ptr);
        bytes[0] = static_cast<unsigned char>(size);
        bytes[size - 1] = static_cast<unsigned char>(size >> 8);
}

double elapsed_ms(Clock::time_point begin, Clock::time_point end) {
        return std::chrono::duration<double, std::milli>(end - begin).count();
}

BenchResult fixed_size_benchmark(const Allocator &allocator, size_t iterations,
                                 size_t size) {
        BenchResult result;
        result.scenario = "fixed-" + std::to_string(size);
        result.allocator = allocator.name;
        result.operations = iterations * 2;
        result.rss_before = current_rss_bytes();

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
        std::mt19937 rng(20260715);
        std::uniform_int_distribution<size_t> dist(0, std::size(kSizes) - 1);

        auto begin = Clock::now();
        for (size_t i = 0; i < iterations; ++i) {
                size_t size = kSizes[dist(rng)];
                ptrs[i] = allocator.alloc(size);
                touch_memory(ptrs[i], size);
        }
        result.rss_middle = current_rss_bytes();

        std::shuffle(ptrs.begin(), ptrs.end(), rng);
        for (void *ptr : ptrs) {
                allocator.free(ptr);
        }
        auto end = Clock::now();

        result.rss_after = current_rss_bytes();
        result.elapsed_ms = elapsed_ms(begin, end);
        return result;
}

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

        for (size_t i = 0; i < iterations; i += 2) {
                allocator.free(ptrs[i]);
                ptrs[i] = nullptr;
        }
        result.rss_middle = current_rss_bytes();

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

void print_result(const BenchResult &result) {
        double seconds = result.elapsed_ms / 1000.0;
        double ops_per_sec = seconds > 0.0 ? result.operations / seconds : 0.0;

        std::cout << result.scenario << ',' << result.allocator << ','
                  << result.operations << ',' << result.elapsed_ms << ','
                  << ops_per_sec << ',' << result.rss_before << ','
                  << result.rss_middle << ',' << result.rss_after << '\n';
}

void run_allocator(const Allocator &allocator, size_t iterations) {
        print_result(fixed_size_benchmark(allocator, iterations, 64));
        print_result(fixed_size_benchmark(allocator, iterations, 1024));
        print_result(mixed_size_benchmark(allocator, iterations));
        print_result(fragmentation_benchmark(allocator, iterations / 2));
        print_result(threaded_benchmark(allocator, iterations));
}

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
        FLAGS_logtostderr = true;
        FLAGS_minloglevel = 2;

        size_t iterations = 20000;
        std::string allocator_name = "all";

        for (int i = 1; i < argc; ++i) {
                std::string value;
                if (has_arg_value(argv[i], "--iterations=", value)) {
                        iterations = std::stoull(value);
                } else if (has_arg_value(argv[i], "--allocator=", value)) {
                        allocator_name = value;
                }
        }

        Allocator malloc_allocator{"malloc", malloc_alloc, malloc_free};
        Allocator tc_allocator{"tcmalloc", tc_alloc, tc_free};

        std::cout << "scenario,allocator,operations,elapsed_ms,ops_per_sec,"
                     "rss_before,rss_middle,rss_after\n";

        if (allocator_name == "malloc") {
                run_allocator(malloc_allocator, iterations);
        } else if (allocator_name == "tcmalloc") {
                run_allocator(tc_allocator, iterations);
        } else {
                run_allocator(malloc_allocator, iterations);
                run_allocator(tc_allocator, iterations);
        }

        return 0;
}
