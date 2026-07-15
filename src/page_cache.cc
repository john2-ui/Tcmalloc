/**
 * @file page_cache.cc
 * @author john2-ui (1463686156@qq.com)
 * @brief 该文件实现页缓存
 * @version 0.1
 * @date 2026-07-15
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "../include/page_cache.hpp"

span *page_cache::new_span(size_t k) {
        if (k == 0) {
                LOG(ERROR) << "page_cache::new_span ignored zero pages";
                return nullptr;
        }

        LOG(INFO) << "page_cache::new_span request, pages: " << k;

        // 大于page
        // cache常规桶范围的span不进入空闲桶，直接向系统申请并在释放时归还系统。
        if (k > PAGES_NUM - 1) {
                void *ptr = system_alloc(k);
                if (ptr == nullptr) {
                        LOG(ERROR)
                            << "page_cache::new_span system_alloc failed, "
                               "pages: "
                            << k;
                        return nullptr;
                }

                span *cur_span = span_pool_.new_();
                if (cur_span == nullptr) {
                        LOG(ERROR)
                            << "page_cache::new_span span allocation failed";
                        system_free(ptr, k << PAGE_SHIFT);
                        return nullptr;
                }

                cur_span->page_id_ = (PAGE_ID)ptr >> PAGE_SHIFT;
                cur_span->page_num_ = k;
                cur_span->is_use_ = true;

                if (!id_span_map_.Ensure(cur_span->page_id_,
                                         cur_span->page_num_)) {
                        LOG(ERROR)
                            << "page_cache::new_span Ensure failed, page_id: "
                            << cur_span->page_id_
                            << ", page_num: " << cur_span->page_num_;
                        system_free(ptr, k << PAGE_SHIFT);
                        span_pool_.delete_(cur_span);
                        return nullptr;
                }

                for (PAGE_ID i = 0; i < cur_span->page_num_; ++i) {
                        id_span_map_.set(cur_span->page_id_ + i, cur_span);
                }

                LOG(INFO) << "page_cache::new_span large result, span: "
                          << cur_span << ", page_id: " << cur_span->page_id_
                          << ", pages: " << cur_span->page_num_;
                return cur_span;
        }

        if (!span_lists_[k].empty()) {
                span *s = span_lists_[k].pop_front();
                if (s == nullptr) {
                        LOG(ERROR)
                            << "page_cache::new_span bucket pop returned null, "
                               "pages: "
                            << k;
                        return nullptr;
                }

                s->is_use_ = true;
                // 重新交给central
                // cache使用前，恢复该span覆盖的每一页到span的映射。
                if (!id_span_map_.Ensure(s->page_id_, s->page_num_)) {
                        LOG(ERROR)
                            << "page_cache::new_span Ensure failed for cached "
                               "span, page_id: "
                            << s->page_id_ << ", pages: " << s->page_num_;
                        s->is_use_ = false;
                        span_lists_[k].push_front(s);
                        return nullptr;
                }

                for (PAGE_ID i = 0; i < s->page_num_; ++i) {
                        id_span_map_.set(s->page_id_ + i, s);
                }
                LOG(INFO) << "page_cache::new_span reuse bucket span, span: "
                          << s << ", pages: " << s->page_num_;
                return s;
        }

        // 第k个桶是空的，去检查后面的桶里面有无span，如果有，可以把它进行切分
        for (size_t i = k + 1; i < PAGES_NUM; ++i) {
                if (!span_lists_[i].empty()) {
                        span *n_s = span_lists_[i].pop_front();
                        if (n_s == nullptr) {
                                LOG(ERROR)
                                    << "page_cache::new_span pop larger bucket "
                                       "returned null, bucket: "
                                    << i;
                                return nullptr;
                        }

                        span *k_s = span_pool_.new_();
                        if (k_s == nullptr) {
                                LOG(ERROR) << "page_cache::new_span split span "
                                              "allocation failed";
                                span_lists_[i].push_front(n_s);
                                return nullptr;
                        }

                        // 在n_s头部切除k页下来
                        k_s->page_id_ = n_s->page_id_;
                        k_s->page_num_ = k;
                        k_s->is_use_ = true;

                        n_s->page_id_ += k;
                        n_s->page_num_ -= k;
                        n_s->is_use_ = false;

                        span_lists_[n_s->page_num_].push_front(n_s);

                        // 存储n_span的首尾页号跟n_span的映射，方便pc回收内存时进行合并查找
                        if (!id_span_map_.Ensure(k_s->page_id_,
                                                 k_s->page_num_) ||
                            !id_span_map_.Ensure(n_s->page_id_,
                                                 n_s->page_num_)) {
                                LOG(ERROR)
                                    << "page_cache::new_span Ensure failed "
                                       "after split";
                                span_lists_[n_s->page_num_].erase(n_s);
                                span_pool_.delete_(k_s);
                                span_lists_[i].push_front(n_s);
                                return nullptr;
                        }

                        id_span_map_.set(n_s->page_id_, n_s);
                        id_span_map_.set(n_s->page_id_ + n_s->page_num_ - 1,
                                         n_s);

                        for (PAGE_ID j = 0; j < k_s->page_num_; ++j) {
                                id_span_map_.set(k_s->page_id_ + j, k_s);
                        }

                        LOG(INFO) << "page_cache::new_span split result, span: "
                                  << k_s << ", pages: " << k_s->page_num_
                                  << ", remain_span: " << n_s
                                  << ", remain_pages: " << n_s->page_num_;
                        return k_s;
                }
        }

        // 所有常规桶为空时，先补充一个最大规格span，再递归走上面的切分逻辑。
        void *ptr = system_alloc(PAGES_NUM - 1);
        if (ptr == nullptr) {
                LOG(ERROR) << "page_cache::new_span system_alloc failed for "
                              "bucket refill";
                return nullptr;
        }

        span *big_span = span_pool_.new_();
        if (big_span == nullptr) {
                LOG(ERROR)
                    << "page_cache::new_span span allocation failed for refill";
                system_free(ptr, (PAGES_NUM - 1) << PAGE_SHIFT);
                return nullptr;
        }

        big_span->page_id_ = (PAGE_ID)ptr >> PAGE_SHIFT;
        big_span->page_num_ = PAGES_NUM - 1;
        big_span->is_use_ = false;

        if (!id_span_map_.Ensure(big_span->page_id_, big_span->page_num_)) {
                LOG(ERROR) << "page_cache::new_span Ensure failed for refill, "
                              "page_id: "
                           << big_span->page_id_
                           << ", pages: " << big_span->page_num_;
                system_free(ptr, (PAGES_NUM - 1) << PAGE_SHIFT);
                span_pool_.delete_(big_span);
                return nullptr;
        }

        span_lists_[PAGES_NUM - 1].push_front(big_span);
        id_span_map_.set(big_span->page_id_, big_span);
        id_span_map_.set(big_span->page_id_ + big_span->page_num_ - 1,
                         big_span);
        LOG(INFO) << "page_cache::new_span refilled largest bucket, span: "
                  << big_span << ", page_id: " << big_span->page_id_;

        return new_span(k);
}

span *page_cache::map_obj_to_span(void *obj) {
        if (obj == nullptr) {
                LOG(WARNING) << "page_cache::map_obj_to_span ignored nullptr";
                return nullptr;
        }

        PAGE_ID id = (PAGE_ID)obj >> PAGE_SHIFT;
        span *s = (span *)id_span_map_.get(id);
        if (s == nullptr) {
                LOG(ERROR) << "page_cache::map_obj_to_span failed, obj: " << obj
                           << ", page_id: " << id;
                return nullptr;
        }
        LOG(INFO) << "page_cache::map_obj_to_span result, obj: " << obj
                  << ", span: " << s;
        return s;
}

void page_cache::release_span_to_page(span *s, size_t size) {
        if (s == nullptr) {
                LOG(WARNING)
                    << "page_cache::release_span_to_page ignored nullptr";
                return;
        }

        if (s->page_num_ == 0) {
                LOG(ERROR) << "page_cache::release_span_to_page invalid span "
                              "pages, span: "
                           << s;
                return;
        }

        LOG(INFO) << "page_cache::release_span_to_page begin, span: " << s
                  << ", page_id: " << s->page_id_
                  << ", pages: " << s->page_num_;

        if (s->page_num_ >= PAGES_NUM) {
                void *ptr = (void *)(s->page_id_ << PAGE_SHIFT);
                size_t free_size =
                    size == 0 ? (s->page_num_ << PAGE_SHIFT) : size;
                if (ptr == nullptr || free_size == 0) {
                        LOG(ERROR)
                            << "page_cache::release_span_to_page invalid large "
                               "span release, ptr: "
                            << ptr << ", size: " << free_size;
                        return;
                }

                if (id_span_map_.Ensure(s->page_id_, s->page_num_)) {
                        for (PAGE_ID i = 0; i < s->page_num_; ++i) {
                                id_span_map_.set(s->page_id_ + i, nullptr);
                        }
                }

                system_free(ptr, free_size);
                span_pool_.delete_(s);
                LOG(INFO) << "page_cache::release_span_to_page freed large "
                             "span, ptr: "
                          << ptr << ", size: " << free_size;
                return;
        }

        s->is_use_ = false;

        // 对span前后页尝试合并，缓解内存碎片问题。
        // page
        // cache只为可合并span保存首尾页映射，因此查找相邻span时只查边界页。
        while (s->page_id_ > 0) {
                PAGE_ID prev_id = s->page_id_ - 1;
                auto ret = (span *)id_span_map_.get(prev_id);
                if (ret == nullptr)
                        break;
                span *prev_span = (span *)ret;
                if (prev_span->is_use_ == true)
                        break;
                if (prev_span->page_num_ + s->page_num_ > PAGES_NUM - 1)
                        break;

                LOG(INFO) << "page_cache::release_span_to_page merge prev, "
                             "prev_span: "
                          << prev_span
                          << ", prev_pages: " << prev_span->page_num_;
                s->page_id_ = prev_span->page_id_;
                s->page_num_ += prev_span->page_num_;

                span_lists_[prev_span->page_num_].erase(prev_span);
                id_span_map_.set(prev_span->page_id_, nullptr);
                id_span_map_.set(prev_span->page_id_ + prev_span->page_num_ - 1,
                                 nullptr);
                span_pool_.delete_(prev_span);
        }

        while (true) {
                PAGE_ID next_id = s->page_id_ + s->page_num_;
                auto ret = (span *)id_span_map_.get(next_id);
                if (ret == nullptr)
                        break;
                span *next_span = (span *)ret;
                if (next_span->is_use_ == true)
                        break;
                if (next_span->page_num_ + s->page_num_ > PAGES_NUM - 1)
                        break;

                LOG(INFO) << "page_cache::release_span_to_page merge next, "
                             "next_span: "
                          << next_span
                          << ", next_pages: " << next_span->page_num_;
                s->page_num_ += next_span->page_num_;
                span_lists_[next_span->page_num_].erase(next_span);
                id_span_map_.set(next_span->page_id_, nullptr);
                id_span_map_.set(next_span->page_id_ + next_span->page_num_ - 1,
                                 nullptr);
                span_pool_.delete_(next_span);
        }

        if (s->page_num_ >= PAGES_NUM) {
                LOG(ERROR) << "page_cache::release_span_to_page merged span "
                              "too large, pages: "
                           << s->page_num_;
                return;
        }

        span_lists_[s->page_num_].push_front(s);

        if (!id_span_map_.Ensure(s->page_id_, s->page_num_)) {
                LOG(ERROR) << "page_cache::release_span_to_page Ensure failed, "
                              "page_id: "
                           << s->page_id_ << ", pages: " << s->page_num_;
                return;
        }

        id_span_map_.set(s->page_id_, s);
        id_span_map_.set(s->page_id_ + s->page_num_ - 1, s);
        LOG(INFO) << "page_cache::release_span_to_page end, span: " << s
                  << ", page_id: " << s->page_id_
                  << ", pages: " << s->page_num_;
}