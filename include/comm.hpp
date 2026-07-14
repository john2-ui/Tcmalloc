#ifndef COMMON_HPP_
#define COMMON_HPP_

#include <cstddef>
#include <cstdlib>
#include <limits>
#include <sys/mman.h>

#include <glog/logging.h>

static const size_t MAX_BYTES = 256 * 1024; // 256kb
static const size_t BUCKETS_NUM = 208;      // 208 buckets
static const size_t PAGES_NUM = 128;        // page cache设置128个桶
static const size_t PAGE_SHIFT = 13;        // 页大小为8kb

typedef unsigned long long PAGE_ID;
#define SYS_BYTES 64

/**
 * @brief 调用系统分配内存的function
 * @param kpage(size_t) 分配内存的页数（一页8kb）
 * @return void* 分配的内存地址
 * @note 分配的内存地址需要通过munmap释放
 */
inline static void *system_alloc(size_t kpage) {
        if (kpage == 0) {
                LOG(WARNING) << "system_alloc called with zero pages";
                return nullptr;
        }

        if (kpage > (std::numeric_limits<size_t>::max() >> PAGE_SHIFT)) {
                LOG(ERROR) << "system_alloc page count overflow, kpage: "
                           << kpage;
                return nullptr;
        }

        size_t bytes = kpage << PAGE_SHIFT;
        LOG(INFO) << "system_alloc request, kpage: " << kpage
                  << ", bytes: " << bytes;

        void *ptr = mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        if (ptr == MAP_FAILED) {
                LOG(ERROR) << "mmap failed, bytes: " << bytes;
                return nullptr;
        }

        return ptr;
}

/**
 * @brief 调用系统释放内存的function
 * @param ptr(void*) 释放内存的地址
 * @param size(size_t) 释放内存的大小（一页8kb）
 * @note 释放内存的地址需要通过munmap释放
 */

inline static void system_free(void *ptr, size_t size = 0) {
        if (ptr == nullptr || size == 0) {
                LOG(WARNING)
                    << "system_free ignored invalid argument, ptr: " << ptr
                    << ", size: " << size;
                return;
        }

        LOG(INFO) << "system_free request, ptr: " << ptr << ", size: " << size;
        int ret = munmap(ptr, size);
        if (ret != 0) {
                LOG(ERROR) << "munmap failed, ptr: " << ptr
                           << ", size: " << size;
                exit(EXIT_FAILURE);
        }
}

/**
 * @brief 获取下一个obj
 * @param obj(void*) 当前obj的地址
 * @return void*& 下一个obj的地址的引用
 *
 * @note 返回地址的引用是为了能够赋值，否则拿到的只是一个右值
 */
inline static void *&next_obj(void *obj) {
        CHECK_NE(obj, nullptr) << "next_obj called with nullptr";
        return *((void **)(obj));
}

/**
 * @brief 单向链表类
 * @note 用于管理从central cache中拿到的空闲内存块
 */
class free_list {
      public:
        /**
         * @brief 将一个obj插入到链表中
         *
         * @param obj(void*) 要插入的obj的地址
         */
        void push(void *obj) {
                if (obj == nullptr) {
                        LOG(WARNING) << "free_list::push ignored nullptr";
                        return;
                }

                LOG(INFO) << "free_list::push before, obj: " << obj
                          << ", size: " << cur_size_;
                next_obj(obj) = free_list_ptr_;
                free_list_ptr_ = obj;
                cur_size_++;
                LOG(INFO) << "free_list::push after, size: " << cur_size_;
        }

        /**
         * @brief 将一段内存插入到链表中
         *
         * @param start(void*) 要插入的内存块的开始地址
         * @param end(void*) 要插入的内存块的结束地址
         * @param n(size_t) 要插入的内存块的数量
         */
        void push(void *start, void *end, size_t n) {
                if (start == nullptr || end == nullptr || n == 0) {
                        LOG(WARNING)
                            << "free_list::push range ignored invalid "
                               "argument, start: "
                            << start << ", end: " << end << ", n: " << n;
                        return;
                }

                LOG(INFO) << "free_list::push range before, start: " << start
                          << ", end: " << end << ", n: " << n
                          << ", size: " << cur_size_;
                next_obj(end) = free_list_ptr_;
                free_list_ptr_ = start;
                cur_size_ += n;
                LOG(INFO) << "free_list::push range after, size: " << cur_size_;
        }

        /**
         * @brief 从链表中获取一个obj
         *
         * @return void* 获取的obj的地址
         */
        void *pop() {
                if (empty()) {
                        LOG(WARNING) << "free_list::pop ignored empty list";
                        return nullptr;
                }

                LOG(INFO) << "free_list::pop before, size: " << cur_size_;
                void *obj = free_list_ptr_;
                free_list_ptr_ = next_obj(free_list_ptr_);
                cur_size_--;
                LOG(INFO) << "free_list::pop after, obj: " << obj
                          << ", size: " << cur_size_;
                return obj;
        }

        /**
         * @brief 从链表中获取一段内存
         *
         * @param start(void*&) 获取的内存块的开始地址的引用
         * @param end(void*&) 获取的内存块的结束地址的引用
         * @param n(size_t) 获取的内存块的数量
         */
        void pop(void *&start, void *&end, size_t n) {
                if (n == 0 || n > cur_size_ || empty()) {
                        LOG(WARNING) << "free_list::pop range ignored invalid "
                                        "argument, n: "
                                     << n << ", size: " << cur_size_;
                        start = nullptr;
                        end = nullptr;
                        return;
                }

                LOG(INFO) << "free_list::pop range before, n: " << n
                          << ", size: " << cur_size_;
                start = free_list_ptr_;
                end = start;
                for (size_t i = 0; i < n - 1; i++) {
                        end = next_obj(end);
                }
                free_list_ptr_ = next_obj(end);
                cur_size_ -= n;
                LOG(INFO) << "free_list::pop range after, start: " << start
                          << ", end: " << end << ", size: " << cur_size_;
        }

        /**
         * @brief 判断链表是否为空
         *
         * @return true 链表为空
         * @return false 链表非空
         */
        bool empty() { return free_list_ptr_ == nullptr; }

        /**
         * @brief 获取链表当前保存的内存块数量
         *
         * @return size_t 当前内存块数量
         */
        size_t size() { return cur_size_; }

        /**
         * @brief 获取链表允许缓存的最大内存块数量
         *
         * @return size_t 最大内存块数量
         */
        size_t max_size() { return max_size_; }

      private:
        /// @brief 链表头
        void *free_list_ptr_ = nullptr;

        /// @brief 链表最大存储容量
        /// @note 在thread cache与central cache之间进行内存块交互时使用
        /// @note 除了标明交互的临界值，还用于thread cache向central
        /// cache申请内存时使用的慢启动反馈调节机制
        size_t max_size_ = 1;

        /// @brief 链表当前存储容量
        size_t cur_size_ = 0;
};

/**
 * @brief 管理对象大小的对齐、桶索引和批量移动规则
 * @note thread cache通过该类把用户请求大小映射到固定规格的free list桶
 */
class size_class {
      public:
        /**
         * @brief 按对象大小所在区间向上对齐
         *
         * @param size(size_t) 要计算的对象大小
         * @return size_t 对齐后的大小，非法或溢出时返回0
         */
        inline static size_t round_up(size_t size) {
                if (size == 0) {
                        LOG(WARNING) << "size_class::round_up got zero size";
                        return 0;
                }

                if (size <= 128) {
                        return round_up_(size, 8);
                } else if (size <= 1024) {
                        return round_up_(size, 16);
                } else if (size <= 8 * 1024) {
                        return round_up_(size, 128);
                } else if (size <= 64 * 1024) {
                        return round_up_(size, 1024);
                } else if (size <= 256 * 1024) {
                        return round_up_(size, 8 * 1024);
                } else {
                        LOG(INFO) << "size_class::round_up handles large "
                                     "allocation, size: "
                                  << size;
                        return round_up_(size, 1 << PAGE_SHIFT);
                }
        }

        /**
         * @brief 计算对象大小的桶索引
         *
         * @param bytes(size_t) 要计算的对象大小
         * @return size_t 桶索引，非法大小返回BUCKETS_NUM作为无效索引
         */
        inline static size_t bucket_index(size_t bytes) {
                if (bytes == 0 || bytes > MAX_BYTES) {
                        LOG(WARNING)
                            << "size_class::bucket_index got invalid bytes: "
                            << bytes;
                        return BUCKETS_NUM;
                }

                LOG(INFO) << "size_class::bucket_index request, bytes: "
                          << bytes;

                // 前4个区间分别拥有的桶数量，用于计算后续区间的偏移量。
                static const size_t group_array[4] = {16, 56, 56, 56};
                if (bytes <= 128) {
                        return bucket_index_(bytes, 3);
                } else if (bytes <= 1024) {
                        return bucket_index_(bytes - 128, 4) + group_array[0];
                } else if (bytes <= 8 * 1024) {
                        return bucket_index_(bytes - 1024, 7) + group_array[1] +
                               group_array[0];
                } else if (bytes <= 64 * 1024) {
                        return bucket_index_(bytes - 8 * 1024, 10) +
                               group_array[2] + group_array[1] + group_array[0];
                } else if (bytes <= 256 * 1024) {
                        return bucket_index_(bytes - 64 * 1024, 13) +
                               group_array[3] + group_array[2] +
                               group_array[1] + group_array[0];
                }

                LOG(ERROR) << "size_class::bucket_index reached unreachable "
                              "branch, bytes: "
                           << bytes;
                return BUCKETS_NUM;
        }

        /**
         * @brief 计算一次threadCache从centralCache获取多少个页
         *
         * @param size(size_t) 要计算的对象大小
         * @return size_t
         * 一次threadCache从centralCache获取多少个页，非法大小返回0
         */
        inline static size_t num_move_page(size_t size) {
                if (size == 0) {
                        LOG(WARNING)
                            << "size_class::num_move_page got zero size";
                        return 0;
                }

                size_t num = num_move_size(size);
                if (num == 0 ||
                    size > std::numeric_limits<size_t>::max() / num) {
                        LOG(ERROR)
                            << "size_class::num_move_page overflow, size: "
                            << size << ", num: " << num;
                        return 0;
                }

                size_t npage = num * size;
                npage >>= PAGE_SHIFT; // 相当于 /= 8kb
                if (npage == 0)
                        npage = 1;

                LOG(INFO) << "size_class::num_move_page result, size: " << size
                          << ", num: " << num << ", npage: " << npage;
                return npage;
        }

        /**
         * @brief 计算一次threadCache从centralCache获取多少个对象
         *
         * @param size(size_t) 要计算的对象大小
         * @return size_t 一次批量移动对象个数，非法大小返回0
         */
        inline static size_t num_move_size(size_t size) {
                if (size == 0) {
                        LOG(WARNING)
                            << "size_class::num_move_size got zero size";
                        return 0;
                }

                // [2, 512]是一次批量移动对象数量的慢启动上限：
                // 小对象一次批量上限高，大对象一次批量上限低。
                size_t num = MAX_BYTES / size;
                if (num < 2)
                        num = 2;
                if (num > 512)
                        num = 512;

                LOG(INFO) << "size_class::num_move_size result, size: " << size
                          << ", num: " << num;
                return num;
        }

      public:
        /**
         * @brief 向上取整
         * @param size(size_t) 要取整的值
         * @param align_size(size_t) 对齐大小
         * @return size_t 取整后的值，非法或溢出时返回0
         */
        inline static size_t round_up_(size_t size, size_t align_size) {
                if (align_size == 0 ||
                    size >
                        std::numeric_limits<size_t>::max() - align_size + 1) {
                        LOG(ERROR)
                            << "size_class::round_up_ invalid argument, size: "
                            << size << ", align_size: " << align_size;
                        return 0;
                }

                return (size + align_size - 1) & ~(align_size - 1);
        }

        /**
         * @brief 计算对象大小的桶索引
         *
         * @param size(size_t) 要计算的对象大小
         * @param align_shift(size_t) 对齐大小
         * @return size_t 桶索引
         */
        inline static size_t bucket_index_(size_t size, size_t align_shift) {
                CHECK_LT(align_shift, sizeof(size_t) * 8)
                    << "invalid align_shift";
                return ((size + (1 << align_shift) - 1) >> align_shift) - 1;
        }
};

#endif // COMMON_HPP_