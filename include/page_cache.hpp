/**
 * @file page_cache.hpp
 * @author john2-ui (1463686156@qq.com)
 * @brief 该文件实现页缓存
 * @version 0.1
 * @date 2026-07-15
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef PAGE_CACHE_HPP
#define PAGE_CACHE_HPP

#include "comm.hpp"
#include "object_pool.hpp"
#include "page_map.hpp"

class page_cache {
      public:
        /**
         * @brief 获取page cache单例
         *
         * @return page_cache& 全局page cache实例
         */
        static page_cache &get_instance() {
                static page_cache instance;
                return instance;
        }

        /**
         * @brief 申请包含k页的span
         *
         * @param k 需要的页数
         * @return span* 申请成功返回span，失败返回nullptr
         */
        span *new_span(size_t k);

        /**
         * @brief 根据对象地址查找其所属span
         *
         * @param obj 对象地址
         * @return span* 成功返回所属span，失败返回nullptr
         */
        span *map_obj_to_span(void *obj);

        /**
         * @brief 将span归还给page cache，并尝试与前后空闲span合并
         *
         * @param s 要归还的span
         * @param size 大span直接归还系统时的字节数；传0时按span页数计算
         */
        void release_span_to_page(span *s, size_t size = 0);

      public:
        /// @brief page cache全局互斥锁
        std::mutex page_mtx_;

      private:
        /// @brief 按页数管理空闲span的桶，下标表示span页数
        span_list span_lists_[PAGES_NUM];

        /// @brief 页号到span的映射，用于根据对象地址反查span
        TCMalloc_PageMap<SYS_BYTES - PAGE_SHIFT> id_span_map_;

        /// @brief span对象池
        object_pool<span> span_pool_;

      private:
        page_cache() = default;
        page_cache(const page_cache &) = delete;
        page_cache &operator=(const page_cache &) = delete;
        ~page_cache() = default;
};

#endif