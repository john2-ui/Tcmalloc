/**
 * @file central_cache.hpp
 * @author john2-ui (1463686156@qq.com)
 * @brief 该文件实现central cache
 * @version 0.1
 * @date 2026-07-15
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef CENTRAL_CACHE_HPP
#define CENTRAL_CACHE_HPP

#include "comm.hpp"

/**
 * @brief 线程共享的中心缓存
 * @note central cache按对象大小维护span链表，负责thread cache和page
 * cache之间的批量对象流转。
 */
class central_cache {
      public:
        /**
         * @brief 获取central cache单例
         *
         * @return central_cache& 全局central cache实例
         */
        static central_cache &get_instance() { return s_instance_; }

        /**
         * @brief 从central cache批量获取对象给thread cache
         *
         * @param start 返回对象链表的起始地址
         * @param end 返回对象链表的结束地址
         * @param batch_num 期望获取的对象数量
         * @param size 单个对象大小
         * @return size_t 实际获取的对象数量，失败返回0
         */
        size_t fetch_range_obj(void *&start, void *&end, size_t batch_num,
                               size_t size);

        /**
         * @brief 将对象链表归还给对应span
         *
         * @param start 要归还的对象链表头
         * @param byte_size 单个对象大小
         */
        void release_list_to_span(void *start, size_t byte_size);

      private:
        /// @brief 按对象大小桶管理的span链表
        span_list span_lists_[BUCKETS_NUM];

        /// @brief central cache单例
        static central_cache s_instance_;

        central_cache() = default;
        central_cache(const central_cache &) = delete;
        central_cache &operator=(const central_cache &) = delete;

        /**
         * @brief 获取指定桶中的非空span；没有可用span时从page cache申请并切分
         *
         * @param list 当前对象大小对应的span链表
         * @param size 单个对象大小
         * @param bucket_lock 当前桶锁，进入page cache前会临时释放
         * @return span* 成功返回非空span，失败返回nullptr
         */
        span *get_non_empty_span(span_list &list, size_t size,
                                 std::unique_lock<std::mutex> &bucket_lock);
};

#endif // CENTRAL_CACHE_HPP