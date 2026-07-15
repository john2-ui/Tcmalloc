/**
 * @file page_map.hpp
 * @author john2-ui (1463686156@qq.com)
 * @brief 该文件实现从页号到span的映射关系
 * @version 0.1
 * @date 2026-07-15
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef PAGE_MAP_HPP
#define PAGE_MAP_HPP

#include "comm.hpp"
#include "object_pool.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

/**
 * @brief 该类使用基数树实现从页号到span的映射关系
 * @note 基数树是一种多级索引结构，用于快速查找和插入数据
 * @tparam BITS 表示可以映射2^BITS个页号
 */
template <int BITS> class TCMalloc_PageMap {

      public:
        typedef uintptr_t Number;

        /**
         * @brief 构造空的页号映射表
         * @note 根节点创建失败时，后续操作会安全失败并打印日志
         */
        TCMalloc_PageMap() {
                root_ = NewNode();
                if (root_ == nullptr) {
                        LOG(ERROR) << "TCMalloc_PageMap init failed";
                        return;
                }

                LOG(INFO) << "TCMalloc_PageMap initialized, bits: " << BITS
                          << ", root: " << root_;
        }

        /**
         * @brief 回收映射表申请过的所有基数树节点
         */
        ~TCMalloc_PageMap() {
                LOG(INFO) << "TCMalloc_PageMap destroyed, allocated nodes: "
                          << allocated_nodes_.size();
                for (auto it = allocated_nodes_.rbegin();
                     it != allocated_nodes_.rend(); ++it) {
                        NodePool().delete_(*it);
                }

                root_ = nullptr;
                allocated_nodes_.clear();
        }

        TCMalloc_PageMap(const TCMalloc_PageMap &) = delete;
        TCMalloc_PageMap &operator=(const TCMalloc_PageMap &) = delete;

        /**
         * @brief 查询页号对应的span指针
         *
         * @param k 页号
         * @return void* 已设置的span指针；未设置或非法页号返回nullptr
         */
        void *get(Number k) const {
                if (!is_valid_key(k)) {
                        LOG(WARNING)
                            << "TCMalloc_PageMap::get invalid key: " << k;
                        return nullptr;
                }

                if (root_ == nullptr) {
                        LOG(ERROR) << "TCMalloc_PageMap::get with null root";
                        return nullptr;
                }

                const Number i1 = k >> (LEAF_BITS + INTERIOR_BITS);
                const Number i2 = (k >> LEAF_BITS) & (INTERIOR_LENGTH - 1);
                const Number i3 = k & (LEAF_LENGTH - 1);
                if (root_->ptrs[i1] == nullptr ||
                    root_->ptrs[i1]->ptrs[i2] == nullptr) {
                        LOG(INFO) << "TCMalloc_PageMap::get miss, key: " << k;
                        return nullptr;
                }

                void *value =
                    reinterpret_cast<Leaf *>(root_->ptrs[i1]->ptrs[i2])
                        ->values[i3];
                LOG(INFO) << "TCMalloc_PageMap::get hit, key: " << k
                          << ", value: " << value;
                return value;
        }

        /**
         * @brief 设置页号到span的映射
         * @note
         * 调用前需要先通过Ensure确保对应节点已存在；v为nullptr时表示清空映射
         *
         * @param k 页号
         * @param v 要保存的span指针
         */
        void set(Number k, void *v) {
                if (!is_valid_key(k)) {
                        LOG(WARNING)
                            << "TCMalloc_PageMap::set invalid key: " << k;
                        return;
                }

                if (root_ == nullptr) {
                        LOG(ERROR) << "TCMalloc_PageMap::set with null root";
                        return;
                }

                const Number i1 = k >> (LEAF_BITS + INTERIOR_BITS);
                const Number i2 = (k >> LEAF_BITS) & (INTERIOR_LENGTH - 1);
                const Number i3 = k & (LEAF_LENGTH - 1);
                if (root_->ptrs[i1] == nullptr ||
                    root_->ptrs[i1]->ptrs[i2] == nullptr) {
                        LOG(WARNING) << "TCMalloc_PageMap::set missing node, "
                                        "call Ensure first, key: "
                                     << k;
                        return;
                }

                reinterpret_cast<Leaf *>(root_->ptrs[i1]->ptrs[i2])
                    ->values[i3] = v;
                LOG(INFO) << "TCMalloc_PageMap::set, key: " << k
                          << ", value: " << v;
        }

        /**
         * @brief 确保[start, start + n)范围内的页号都能建立映射
         *
         * @param start 起始页号
         * @param n 页数
         * @return true 节点准备完成
         * @return false 参数非法或节点申请失败
         */
        bool Ensure(Number start, size_t n) {
                if (n == 0) {
                        LOG(INFO) << "TCMalloc_PageMap::Ensure empty range";
                        return true;
                }

                if (!is_valid_range(start, n)) {
                        LOG(WARNING)
                            << "TCMalloc_PageMap::Ensure invalid range, start: "
                            << start << ", n: " << n;
                        return false;
                }

                if (root_ == nullptr) {
                        LOG(ERROR) << "TCMalloc_PageMap::Ensure with null root";
                        return false;
                }

                const Number end = start + static_cast<Number>(n) - 1;
                LOG(INFO) << "TCMalloc_PageMap::Ensure range, start: " << start
                          << ", end: " << end << ", n: " << n;

                for (Number key = start; key <= end;) {
                        const Number i1 = key >> (LEAF_BITS + INTERIOR_BITS);
                        const Number i2 =
                            (key >> LEAF_BITS) & (INTERIOR_LENGTH - 1);

                        if (i1 >= INTERIOR_LENGTH || i2 >= INTERIOR_LENGTH) {
                                LOG(ERROR)
                                    << "TCMalloc_PageMap::Ensure index out of "
                                       "range, key: "
                                    << key << ", i1: " << i1 << ", i2: " << i2;
                                return false;
                        }

                        if (root_->ptrs[i1] == nullptr) {
                                Node *node = NewNode();
                                if (node == nullptr) {
                                        return false;
                                }
                                root_->ptrs[i1] = node;
                                LOG(INFO) << "TCMalloc_PageMap::Ensure created "
                                             "interior node, i1: "
                                          << i1 << ", node: " << node;
                        }

                        if (root_->ptrs[i1]->ptrs[i2] == nullptr) {
                                Node *node = NewNode();
                                if (node == nullptr) {
                                        return false;
                                }
                                root_->ptrs[i1]->ptrs[i2] = node;
                                LOG(INFO)
                                    << "TCMalloc_PageMap::Ensure created leaf "
                                       "node, i1: "
                                    << i1 << ", i2: " << i2
                                    << ", node: " << node;
                        }

                        Number next_key = ((key >> LEAF_BITS) + 1) << LEAF_BITS;
                        if (next_key <= key || next_key > end) {
                                break;
                        }
                        key = next_key;
                }

                return true;
        }

      private:
        /// @brief Interior node bits
        static constexpr int INTERIOR_BITS = (BITS + 2) / 3;
        static constexpr Number INTERIOR_LENGTH = Number{1} << INTERIOR_BITS;

        /// @brief Leaf node bits
        static constexpr int LEAF_BITS = BITS - 2 * INTERIOR_BITS;
        static constexpr Number LEAF_LENGTH = Number{1} << LEAF_BITS;

        static_assert(BITS >= 2, "BITS must provide at least two page bits");
        static_assert(BITS < static_cast<int>(sizeof(Number) * 8),
                      "BITS must be smaller than Number width");
        static_assert(LEAF_BITS >= 0, "LEAF_BITS must not be negative");

        /// @brief Interior node
        struct Node {
                Node *ptrs[INTERIOR_LENGTH];
        };

        /// @brief Leaf node
        struct Leaf {
                void *values[LEAF_LENGTH];
        };

        // 当前实现用NodePool统一申请节点存储，叶子层再按Leaf视图访问；
        // 因此要求Node的空间至少能容纳Leaf。
        static_assert(sizeof(Node) >= sizeof(Leaf),
                      "Node storage must be large enough for Leaf");

        /// @brief Root node
        Node *root_ = nullptr;

        /// @brief 记录所有从对象池申请的节点，析构时统一归还
        std::vector<Node *> allocated_nodes_;

      private:
        /**
         * @brief 获取用于分配基数树节点的对象池
         *
         * @return object_pool<Node>& 节点对象池
         */
        static object_pool<Node> &NodePool() {
                static object_pool<Node> node_pool;
                return node_pool;
        }

        /**
         * @brief 判断单个页号是否在当前PageMap可映射范围内
         *
         * @param key 页号
         * @return true 页号有效
         * @return false 页号越界
         */
        static bool is_valid_key(Number key) { return (key >> BITS) == 0; }

        /**
         * @brief 判断页号范围是否完全在当前PageMap可映射范围内
         *
         * @param start 起始页号
         * @param n 页数
         * @return true 范围有效
         * @return false 范围为空以外的非法情况，包括溢出和越界
         */
        static bool is_valid_range(Number start, size_t n) {
                if (!is_valid_key(start)) {
                        return false;
                }

                Number max_key = (Number{1} << BITS) - 1;
                Number count = static_cast<Number>(n);
                if (count == 0 || count - 1 > max_key - start) {
                        return false;
                }

                return true;
        }

        /**
         * @brief 从对象池申请并清零一个基数树节点
         *
         * @return Node* 成功返回节点地址，失败返回nullptr
         */
        Node *NewNode() {
                Node *result = NodePool().new_();
                if (result == nullptr) {
                        LOG(ERROR) << "TCMalloc_PageMap::NewNode failed";
                        return nullptr;
                }
                memset(result, 0, sizeof(Node));
                allocated_nodes_.push_back(result);
                LOG(INFO) << "TCMalloc_PageMap::NewNode result: " << result;
                return result;
        }
};

#endif