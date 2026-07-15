/**
 * @file object_pool.hpp
 * @author john2-ui (1463686156@qq.com)
 * @brief 该文件用于定义对象池类,用于消除对new和delete的依赖
 * @version 0.1
 * @date 2026-07-15
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef OBJECT_POOL_HPP
#define OBJECT_POOL_HPP

#include <new>
#include <utility>
#include <vector>

#include "comm.hpp"

static const size_t DEFAULT_KB = 128; // 默认一次向系统申请128KB

/**
 * @brief 简单对象池，用于批量申请对象存储并复用释放后的对象空间
 *
 * @tparam T 对象池管理的对象类型
 * @note
 * new_会在对象池内存上调用T的构造函数，delete_会调用析构函数并把空间挂回空闲链表
 */
template <class T> class object_pool {
      public:
        object_pool() = default;
        object_pool(const object_pool &) = delete;
        object_pool &operator=(const object_pool &) = delete;

        /**
         * @brief 从对象池中申请并构造一个对象
         *
         * @tparam Args 构造T时传入的参数类型
         * @param args 构造T时传入的参数
         * @return T* 构造成功的对象指针，失败返回nullptr
         */
        template <class... Args> T *new_(Args &&...args) {
                T *obj = nullptr;

                if (free_list_ != nullptr) {
                        LOG(INFO) << "object_pool::new_ reuse object from "
                                     "free_list, free_list: "
                                  << free_list_;
                        obj = static_cast<T *>(free_list_);
                        void *next = *((void **)free_list_);
                        free_list_ = next;
                        return construct_object_(obj,
                                                 std::forward<Args>(args)...);
                }

                size_t obj_size = object_size_();
                if (remain_size_ < obj_size) {
                        if (!allocate_chunk_()) {
                                return nullptr;
                        }
                }

                LOG(INFO) << "object_pool::new_ allocate object from chunk, "
                             "memory: "
                          << static_cast<void *>(memory_)
                          << ", remain_size: " << remain_size_
                          << ", obj_size: " << obj_size;
                obj = reinterpret_cast<T *>(memory_);
                memory_ += obj_size;
                remain_size_ -= obj_size;
                return construct_object_(obj, std::forward<Args>(args)...);
        }

        /**
         * @brief 销毁对象并把其空间归还到对象池空闲链表
         *
         * @param obj 要释放的对象指针，nullptr会被忽略
         */
        void delete_(T *obj) {
                if (obj == nullptr) {
                        LOG(WARNING) << "object_pool::delete_ ignored nullptr";
                        return;
                }

                LOG(INFO) << "object_pool::delete_ recycle object, obj: "
                          << obj;
                obj->~T();
                *((void **)obj) = free_list_;
                free_list_ = obj;
        }

        /**
         * @brief 释放对象池向系统申请的所有内存块
         * @note 对象池销毁前，仍存活对象应由调用方先通过delete_归还
         */
        ~object_pool() {
                for (const auto &chunk : chunks_) {
                        LOG(INFO)
                            << "object_pool::~object_pool free chunk, ptr: "
                            << chunk.first << ", size: " << chunk.second;
                        system_free(chunk.first, chunk.second);
                }
        }

      private:
        /**
         * @brief 计算单个对象在对象池中占用的空间
         *
         * @return size_t 向上对齐后的对象空间大小
         */
        static size_t object_size_() {
                size_t size =
                    sizeof(T) < sizeof(void *) ? sizeof(void *) : sizeof(T);
                size_t align =
                    alignof(T) < alignof(void *) ? alignof(void *) : alignof(T);
                return (size + align - 1) & ~(align - 1);
        }

        /**
         * @brief 向系统申请新的内存块
         * @note
         * 默认按128KB批量申请；如果单个T对象更大，则按对象大小向上按页申请，
         * 避免对象池把大对象写出chunk边界。
         *
         * @return true 申请成功
         * @return false 申请失败
         */
        bool allocate_chunk_() {
                size_t obj_size = object_size_();
                // 如果对象本身比默认 chunk 大，就按对象大小申请足够的页数
                size_t request_bytes =
                    obj_size > chunk_bytes_ ? obj_size : chunk_bytes_;
                size_t page_bytes = size_t{1} << PAGE_SHIFT;
                size_t kpage = (request_bytes + page_bytes - 1) >> PAGE_SHIFT;
                size_t actual_bytes = kpage << PAGE_SHIFT;
                LOG(INFO) << "object_pool::allocate_chunk_ request, kpage: "
                          << kpage << ", chunk_bytes: " << actual_bytes
                          << ", obj_size: " << obj_size;

                char *chunk = static_cast<char *>(system_alloc(kpage));
                if (chunk == nullptr) {
                        LOG(ERROR) << "object_pool::allocate_chunk_ failed";
                        return false;
                }

                chunks_.push_back({chunk, actual_bytes});
                memory_ = chunk;
                remain_size_ = actual_bytes;
                return true;
        }

        /**
         * @brief 在对象池内存上构造对象
         *
         * @tparam Args 构造T时传入的参数类型
         * @param obj 对象内存地址
         * @param args 构造T时传入的参数
         * @return T* 构造完成的对象地址，失败返回nullptr
         */
        template <class... Args> T *construct_object_(T *obj, Args &&...args) {
                if (obj == nullptr) {
                        LOG(ERROR)
                            << "object_pool::construct_object_ got nullptr";
                        return nullptr;
                }

                try {
                        return new (obj) T(std::forward<Args>(args)...);
                } catch (...) {
                        LOG(ERROR)
                            << "object_pool::construct_object_ constructor "
                               "threw, obj: "
                            << obj;
                        *((void **)obj) = free_list_;
                        free_list_ = obj;
                        return nullptr;
                }
        }

        /// @brief 当前内存块中下一次可分配的位置
        char *memory_ = nullptr;

        /// @brief 当前内存块剩余可分配字节数
        size_t remain_size_ = 0;

        /// @brief 归还对象组成的空闲链表
        void *free_list_ = nullptr;

        /// @brief 对象池持有的系统内存块，析构时统一归还
        std::vector<std::pair<void *, size_t>> chunks_;

        /// @brief 每次向系统申请的内存块大小
        static constexpr size_t chunk_bytes_ = DEFAULT_KB * 1024;
};

#endif