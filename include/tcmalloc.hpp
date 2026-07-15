/**
 * @file tcmalloc.hpp
 * @author john2-ui (1463686156@qq.com)
 * @brief 对外暴露tcmalloc/tcfree接口
 * @version 0.1
 * @date 2026-07-15
 *
 * @copyright Copyright (c) 2026
 *
 */
#ifndef TCMALLOC_HPP
#define TCMALLOC_HPP

#include "comm.hpp"
#include "object_pool.hpp"
#include "page_cache.hpp"
#include "thread_cache.hpp"
#include <cstddef>
#include <mutex>

/**
 * @brief 获取当前线程的thread cache，首次使用时延迟创建
 *
 * @return thread_cache* 成功返回当前线程缓存，失败返回nullptr
 */
static thread_cache *get_tls_thread_cache() {
        if (p_tls_thread_cache != nullptr) {
                return p_tls_thread_cache;
        }

        // thread cache本身是线程局部的，但用于创建它的对象池是进程内共享的。
        // 多线程首次分配时需要保护对象池，避免并发修改free list/chunk状态。
        static object_pool<thread_cache> thread_cache_pool;
        static std::mutex thread_cache_pool_mtx;
        std::lock_guard<std::mutex> lock(thread_cache_pool_mtx);

        if (p_tls_thread_cache == nullptr) {
                p_tls_thread_cache = thread_cache_pool.new_();
                if (p_tls_thread_cache == nullptr) {
                        LOG(ERROR) << "failed to allocate thread cache";
                        return nullptr;
                }
                LOG(INFO) << "initialized thread cache, ptr: "
                          << p_tls_thread_cache;
        }

        return p_tls_thread_cache;
}

/**
 * @brief 申请指定大小的内存
 *
 * @param size 要申请的字节数
 * @return void* 成功返回可用内存地址，失败返回nullptr
 */
static void *tcmalloc(size_t size) {
        if (size == 0) {
                LOG(WARNING) << "tcmalloc ignored zero size";
                return nullptr;
        }

        LOG(INFO) << "tcmalloc request, size: " << size;

        if (size > MAX_BYTES) {
                size_t align_size = size_class::round_up(size);
                if (align_size == 0) {
                        LOG(ERROR) << "tcmalloc failed to round up large size: "
                                   << size;
                        return nullptr;
                }

                size_t kpage = align_size >> PAGE_SHIFT;
                if (kpage == 0) {
                        LOG(ERROR) << "tcmalloc got zero kpage, size: " << size
                                   << ", align_size: " << align_size;
                        return nullptr;
                }

                std::unique_lock<std::mutex> page_lock(
                    page_cache::get_instance().page_mtx_);
                span *cur_span = page_cache::get_instance().new_span(kpage);
                if (cur_span == nullptr) {
                        LOG(ERROR)
                            << "tcmalloc page cache allocation failed, kpage: "
                            << kpage << ", size: " << size;
                        return nullptr;
                }

                cur_span->obj_size_ = align_size;
                cur_span->is_use_ = true;
                void *ptr = (void *)(cur_span->page_id_ << PAGE_SHIFT);
                LOG(INFO) << "tcmalloc large allocation result, ptr: " << ptr
                          << ", align_size: " << align_size
                          << ", kpage: " << kpage;
                return ptr;
        }

        thread_cache *cache = get_tls_thread_cache();
        if (cache == nullptr) {
                return nullptr;
        }

        void *ptr = cache->allocate(size);
        if (ptr == nullptr) {
                LOG(ERROR) << "tcmalloc small allocation failed, size: "
                           << size;
        }
        return ptr;
}

/**
 * @brief 释放tcmalloc申请的内存
 *
 * @param ptr 要释放的内存地址，nullptr会被忽略
 */
static void tcfree(void *ptr) {
        if (ptr == nullptr) {
                LOG(WARNING) << "tcfree ignored nullptr";
                return;
        }

        LOG(INFO) << "tcfree request, ptr: " << ptr;

        std::unique_lock<std::mutex> page_lock(
            page_cache::get_instance().page_mtx_);
        span *s = page_cache::get_instance().map_obj_to_span(ptr);
        if (s == nullptr) {
                LOG(ERROR) << "tcfree failed to map ptr to span, ptr: " << ptr;
                return;
        }

        size_t size = s->obj_size_;
        if (size == 0) {
                LOG(ERROR) << "tcfree found span with zero obj_size, ptr: "
                           << ptr << ", span: " << s;
                return;
        }

        if (size > MAX_BYTES) {
                page_cache::get_instance().release_span_to_page(s, size);
                LOG(INFO) << "tcfree released large allocation, ptr: " << ptr
                          << ", size: " << size;
                return;
        }

        page_lock.unlock();

        thread_cache *cache = get_tls_thread_cache();
        if (cache == nullptr) {
                return;
        }

        cache->deallocate(ptr, size);
        LOG(INFO) << "tcfree released small allocation, ptr: " << ptr
                  << ", size: " << size;
}

#endif
