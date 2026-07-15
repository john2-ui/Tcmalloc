/**
 * @file central_cache.cc
 * @author john2-ui (1463686156@qq.com)
 * @brief 该文件实现central cache
 * @version 0.1
 * @date 2026-07-15
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "../include/central_cache.hpp"
#include "../include/page_cache.hpp"
#include <cstddef>
#include <mutex>

size_t central_cache::fetch_range_obj(void *&start, void *&end,
                                      size_t batch_num, size_t size) {
        start = nullptr;
        end = nullptr;

        if (batch_num == 0 || size == 0 || size > MAX_BYTES) {
                LOG(WARNING)
                    << "central_cache::fetch_range_obj invalid argument, "
                       "batch_num: "
                    << batch_num << ", size: " << size;
                return 0;
        }

        size_t index = size_class::bucket_index(size);
        if (index >= BUCKETS_NUM) {
                LOG(ERROR) << "central_cache::fetch_range_obj invalid bucket "
                              "index: "
                           << index << ", size: " << size;
                return 0;
        }

        LOG(INFO) << "central_cache::fetch_range_obj begin, batch_num: "
                  << batch_num << ", size: " << size << ", index: " << index;

        std::unique_lock<std::mutex> bucket_lock(
            span_lists_[index].bucket_mtx_);
        span *span = get_non_empty_span(span_lists_[index], size, bucket_lock);
        if (span == nullptr || span->free_list_ == nullptr) {
                LOG(ERROR)
                    << "central_cache::fetch_range_obj no available span, "
                       "size: "
                    << size << ", span: " << span;
                return 0;
        }

        start = span->free_list_;
        end = start;

        size_t actual_n = 1;
        while (actual_n < batch_num && next_obj(end) != nullptr) {
                end = next_obj(end);
                ++actual_n;
        }

        span->free_list_ = next_obj(end);
        next_obj(end) = nullptr;
        span->use_count_ += actual_n;

        LOG(INFO) << "central_cache::fetch_range_obj end, actual_n: "
                  << actual_n << ", span: " << span
                  << ", use_count: " << span->use_count_;
        return actual_n;
}

span *
central_cache::get_non_empty_span(span_list &list, size_t size,
                                  std::unique_lock<std::mutex> &bucket_lock) {
        if (!bucket_lock.owns_lock()) {
                LOG(ERROR)
                    << "central_cache::get_non_empty_span requires bucket lock";
                return nullptr;
        }

        span *it = list.begin();
        while (it != list.end()) {
                if (it->free_list_ != nullptr) {
                        LOG(INFO)
                            << "central_cache::get_non_empty_span hit cached "
                               "span: "
                            << it;
                        return it;
                }
                it = it->next_;
        }

        // 进入page cache前释放当前size
        // class的桶锁，避免同时持有central桶锁和page全局锁。
        bucket_lock.unlock();

        //没有空闲span，向os申请
        span *cur_span = nullptr;
        {
                std::unique_lock<std::mutex> page_lock(
                    page_cache::get_instance().page_mtx_);
                size_t page_num = size_class::num_move_page(size);
                if (page_num == 0) {
                        LOG(ERROR)
                            << "central_cache::get_non_empty_span invalid "
                               "page_num for size: "
                            << size;
                        bucket_lock.lock();
                        return nullptr;
                }

                LOG(INFO) << "central_cache::get_non_empty_span request page "
                             "cache, page_num: "
                          << page_num << ", size: " << size;
                cur_span = page_cache::get_instance().new_span(page_num);
        }

        if (cur_span == nullptr || cur_span->page_num_ == 0) {
                LOG(ERROR)
                    << "central_cache::get_non_empty_span page cache failed, "
                       "span: "
                    << cur_span;
                bucket_lock.lock();
                return nullptr;
        }

        cur_span->is_use_ = true;
        cur_span->obj_size_ = size;

        // 将page cache返回的连续span切成固定size的小对象链表，供thread
        // cache批量获取。
        char *addr_start = (char *)(cur_span->page_id_ << PAGE_SHIFT);
        size_t bytes = cur_span->page_num_ << PAGE_SHIFT;
        if (addr_start == nullptr || bytes < size) {
                LOG(ERROR)
                    << "central_cache::get_non_empty_span invalid span memory, "
                       "addr_start: "
                    << static_cast<void *>(addr_start) << ", bytes: " << bytes
                    << ", size: " << size;
                bucket_lock.lock();
                return nullptr;
        }

        char *addr_end = addr_start + bytes;

        // 第一个对象作为链表头，后续对象通过对象头部的next指针串起来。
        cur_span->free_list_ = addr_start;
        addr_start += size;
        void *tail = cur_span->free_list_;

        size_t obj_count = 1;
        while (addr_start < addr_end) {
                ++obj_count;
                next_obj(tail) = addr_start;
                tail = next_obj(tail);
                addr_start += size;
        }
        next_obj(tail) = nullptr;

        bucket_lock.lock();
        list.push_front(cur_span);
        LOG(INFO) << "central_cache::get_non_empty_span split span, span: "
                  << cur_span << ", obj_count: " << obj_count
                  << ", obj_size: " << size;
        return cur_span;
}

void central_cache::release_list_to_span(void *start, size_t byte_size) {
        if (start == nullptr) {
                LOG(WARNING)
                    << "central_cache::release_list_to_span ignored nullptr";
                return;
        }

        if (byte_size == 0 || byte_size > MAX_BYTES) {
                LOG(WARNING)
                    << "central_cache::release_list_to_span invalid size: "
                    << byte_size;
                return;
        }

        size_t index = size_class::bucket_index(byte_size);
        if (index >= BUCKETS_NUM) {
                LOG(ERROR)
                    << "central_cache::release_list_to_span invalid bucket "
                       "index: "
                    << index << ", size: " << byte_size;
                return;
        }

        LOG(INFO) << "central_cache::release_list_to_span begin, size: "
                  << byte_size << ", index: " << index;

        std::unique_lock<std::mutex> bucket_lock(
            span_lists_[index].bucket_mtx_);
        while (start != nullptr) {
                void *next = next_obj(start);
                span *cur_span = nullptr;
                {
                        std::unique_lock<std::mutex> page_lock(
                            page_cache::get_instance().page_mtx_);
                        cur_span =
                            page_cache::get_instance().map_obj_to_span(start);
                }

                if (cur_span == nullptr) {
                        LOG(ERROR)
                            << "central_cache::release_list_to_span cannot map "
                               "object: "
                            << start;
                        start = next;
                        continue;
                }

                next_obj(start) = cur_span->free_list_;
                cur_span->free_list_ = start;

                if (cur_span->use_count_ == 0) {
                        LOG(ERROR)
                            << "central_cache::release_list_to_span found "
                               "use_count underflow risk, span: "
                            << cur_span;
                        start = next;
                        continue;
                }

                cur_span->use_count_--;
                LOG(INFO) << "central_cache::release_list_to_span returned "
                             "object, span: "
                          << cur_span
                          << ", use_count: " << cur_span->use_count_;

                if (cur_span->use_count_ == 0) {
                        span_lists_[index].erase(cur_span);
                        cur_span->free_list_ = nullptr;
                        cur_span->next_ = nullptr;
                        cur_span->prev_ = nullptr;

                        // 当前span已经从central桶中摘除，释放桶锁后再归还给page
                        // cache做合并。
                        bucket_lock.unlock();
                        {
                                std::unique_lock<std::mutex> page_lock(
                                    page_cache::get_instance().page_mtx_);
                                page_cache::get_instance().release_span_to_page(
                                    cur_span);
                        }
                        bucket_lock.lock();
                }

                start = next;
        }
        LOG(INFO) << "central_cache::release_list_to_span end";
}