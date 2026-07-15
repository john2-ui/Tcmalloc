#ifndef THREAD_CACHE_HPP
#define THREAD_CACHE_HPP

#include "comm.hpp"

/**
 * @brief threadCache是线程本地缓存，每个线程有一个threadCache实例
 * @note 小对象优先从线程本地free list分配，free list为空时再批量向central
 * cache申请。
 */
class thread_cache {
      public:
        /**
         * @brief 从threadCache中申请内存
         *
         * @param size(size_t) 要申请的内存大小
         * @return void* 申请到的内存地址，失败返回nullptr
         */
        void *allocate(size_t size);

        /**
         * @brief 将内存释放到threadCache中
         *
         * @param ptr(void*) 要释放的内存地址
         * @param size(size_t) 要释放的内存大小
         */
        void deallocate(void *ptr, size_t size);

      private:
        /**
         * @brief 从centralCache中获取内存
         *
         * @param index(size_t) 桶索引
         * @param size(size_t) 要申请的单个内存块大小
         * @return void* 申请到的内存地址，失败返回nullptr
         */
        void *fetch_from_central_cache(size_t index, size_t size);

        /**
         * @brief 将过长的thread cache自由链表归还一批对象给central cache
         * @note
         * 当链表长度大于一次批量申请的内存的时候，就开始还一段list给central
         * cache
         * @param list(free_list&) 要释放的链表
         * @param size(size_t) 要释放的内存大小
         */
        void list_too_long(free_list &list, size_t size);

      private:
        /// @brief 由BUCKETS_NUM个自由链表构成的哈希表（数组）
        free_list free_lists_[BUCKETS_NUM];
};

extern thread_local thread_cache *p_tls_thread_cache;
#endif // THREAD_CACHE_HPP