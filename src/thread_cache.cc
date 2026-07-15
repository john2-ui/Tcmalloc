#include "../include/thread_cache.hpp"
#include "../include/central_cache.hpp"

#include <algorithm>

thread_local thread_cache *p_tls_thread_cache = nullptr;

void *thread_cache::allocate(size_t size) {
        if (size == 0 || size > MAX_BYTES) {
                LOG(ERROR) << "thread_cache::allocate invalid size: " << size;
                return nullptr;
        }

        size_t align_size = size_class::round_up(size);
        if (align_size == 0 || align_size > MAX_BYTES) {
                LOG(ERROR) << "thread_cache::allocate invalid align size: "
                           << align_size;
                return nullptr;
        }

        size_t index = size_class::bucket_index(align_size);
        if (index >= BUCKETS_NUM) {
                LOG(ERROR) << "thread_cache::allocate invalid index: " << index;
                return nullptr;
        }

        LOG(INFO) << "thread_cache::allocate request, size: " << size
                  << ", align_size: " << align_size << ", index: " << index;
        if (!free_lists_[index].empty()) {
                void *obj = free_lists_[index].pop();
                LOG(INFO) << "thread_cache::allocate hit local free list, obj: "
                          << obj;
                return obj;
        }

        return fetch_from_central_cache(index, align_size);
}

void *thread_cache::fetch_from_central_cache(size_t index, size_t size) {
        if (index >= BUCKETS_NUM || size == 0 || size > MAX_BYTES) {
                LOG(ERROR) << "thread_cache::fetch_from_central_cache invalid "
                              "argument, index: "
                           << index << ", size: " << size;
                return nullptr;
        }

        // 慢启动反馈调节算法
        size_t batch_num = std::min(free_lists_[index].max_size(),
                                    size_class::num_move_size(size));
        if (batch_num == 0) {
                LOG(ERROR)
                    << "thread_cache::fetch_from_central_cache got zero batch";
                return nullptr;
        }

        if (free_lists_[index].max_size() == batch_num) {
                free_lists_[index].max_size() += 1;
                LOG(INFO) << "thread_cache::fetch_from_central_cache increase "
                             "max_size, index: "
                          << index
                          << ", max_size: " << free_lists_[index].max_size();
        }

        void *start = nullptr;
        void *end = nullptr;

        size_t actual_n = central_cache::get_instance().fetch_range_obj(
            start, end, batch_num, size);
        if (actual_n == 0) {
                LOG(ERROR) << "thread_cache::fetch_from_central_cache fetch "
                              "range obj failed: "
                           << batch_num << " " << size;
                return nullptr;
        }

        if (start == nullptr || end == nullptr) {
                LOG(ERROR)
                    << "thread_cache::fetch_from_central_cache got invalid "
                       "range, start: "
                    << start << ", end: " << end;
                return nullptr;
        }

        if (actual_n == 1) {
                LOG(INFO) << "thread_cache::fetch_from_central_cache got one "
                             "object, obj: "
                          << start;
                return start;
        }

        void *result = start;
        void *next = next_obj(start);
        next_obj(start) = nullptr;
        if (next != nullptr) {
                free_lists_[index].push(next, end, actual_n - 1);
        }

        LOG(INFO) << "thread_cache::fetch_from_central_cache fetched range, "
                     "actual_n: "
                  << actual_n << ", result: " << result
                  << ", cached: " << (actual_n - 1);
        return result;
}

void thread_cache::deallocate(void *ptr, size_t size) {
        if (ptr == nullptr) {
                LOG(WARNING) << "thread_cache::deallocate ignored nullptr";
                return;
        }

        if (size == 0 || size > MAX_BYTES) {
                LOG(WARNING)
                    << "thread_cache::deallocate ignored invalid size: "
                    << size;
                return;
        }

        size_t align_size = size_class::round_up(size);
        if (align_size == 0 || align_size > MAX_BYTES) {
                LOG(ERROR) << "thread_cache::deallocate invalid align size: "
                           << align_size;
                return;
        }

        size_t index = size_class::bucket_index(align_size);
        if (index >= BUCKETS_NUM) {
                LOG(ERROR) << "thread_cache::deallocate invalid index: "
                           << index;
                return;
        }

        LOG(INFO) << "thread_cache::deallocate request, ptr: " << ptr
                  << ", size: " << size << ", align_size: " << align_size
                  << ", index: " << index;
        free_lists_[index].push(ptr);

        //当链表长度大于一次批量申请的内存的时候，就开始还一段list给cc
        if (free_lists_[index].size() >= free_lists_[index].max_size()) {
                list_too_long(free_lists_[index], align_size);
        }
}

void thread_cache::list_too_long(free_list &list, size_t size) {
        if (size == 0 || size > MAX_BYTES) {
                LOG(WARNING)
                    << "thread_cache::list_too_long invalid size: " << size;
                return;
        }

        if (list.empty() || list.max_size() == 0) {
                LOG(WARNING)
                    << "thread_cache::list_too_long ignored empty list or zero "
                       "max_size";
                return;
        }

        void *start = nullptr;
        void *end = nullptr;
        size_t release_num = std::min(list.size(), list.max_size());
        list.pop(start, end, release_num);
        if (start == nullptr) {
                LOG(ERROR)
                    << "thread_cache::list_too_long pop returned nullptr";
                return;
        }

        if (end != nullptr) {
                next_obj(end) = nullptr;
        }

        LOG(INFO) << "thread_cache::list_too_long release range, start: "
                  << start << ", end: " << end
                  << ", release_num: " << release_num << ", size: " << size;
        central_cache::get_instance().release_list_to_span(start, size);
}