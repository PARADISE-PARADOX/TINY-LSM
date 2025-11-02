#include "../../include/block/block_cache.h"
#include "../../include/block/block.h"
#include <chrono>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace tiny_lsm {
BlockCache::BlockCache(size_t capacity, size_t k)
    : capacity_(capacity), k_(k) {}

BlockCache::~BlockCache() = default;

std::shared_ptr<Block> BlockCache::get(int sst_id, int block_id) {
  // TODO: Lab 4.8 查询一个 Block

  // 设置写锁并自动管理
  std::lock_guard<std::mutex> lock(mutex_);

  //获取缓存
  auto key = std::make_pair(sst_id, block_id);
  auto it = cache_map_.find(key);

  total_requests_++; //增加总请求数

  if (it == cache_map_.end()) {
    return nullptr; // 缓存未命中
  }

  hit_requests_++;

  //更新命中次数
  update_access_count(it->second);

  return it->second->cache_block;
}

void BlockCache::put(int sst_id, int block_id, std::shared_ptr<Block> block) {
  // TODO: Lab 4.8 插入一个 Block

  // 设置写锁并自动管理
  std::lock_guard<std::mutex> lock(mutex_);

  //获取缓存
  auto key = std::make_pair(sst_id, block_id);
  auto it = cache_map_.find(key);

  if (it != cache_map_.end()) {
    // 在缓存内时更新
    it->second->cache_block = block;
    update_access_count(it->second);
  } else {
    if (cache_map_.size() >= capacity_) {
      if (!cache_list_less_k.empty()) {
        auto del_key = std::make_pair(cache_list_less_k.back().sst_id,
                                      cache_list_less_k.back().block_id);
        cache_map_.erase(del_key); //删除小于k链表的末尾项
        cache_list_less_k.pop_back(); // 将最后一项舍弃
      } else {
        auto del_key = std::make_pair(cache_list_greater_k.back().sst_id,
                                      cache_list_greater_k.back().block_id);
        cache_map_.erase(del_key);
        cache_list_greater_k.pop_back(); // 将最后一项舍弃
      }
    }

    //放入链表头部的操作整合到一起
    CacheItem c_item = {sst_id, block_id, block, 1};
    cache_list_less_k.push_front(c_item);
    cache_map_[key] = cache_list_less_k.begin();
  }
}

double BlockCache::hit_rate() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return total_requests_ == 0
             ? 0.0
             : static_cast<double>(hit_requests_) / total_requests_;
}

void BlockCache::update_access_count(std::list<CacheItem>::iterator it) {
  // TODO: Lab 4.8 更新统计信息
  ++it->access_count;
  if (it->access_count < k_) {
    //访问数小于k，将其放置在小于k链表的头部
    cache_list_less_k.splice(cache_list_less_k.begin(), cache_list_less_k, it);
  } else if (it->access_count == k_) {
    // 访问数等于k时，将该项从小于k的链表移动到大于k的链表的头部
    auto item = *it;
    cache_list_less_k.erase(it);   //从小于k的链表删除该元素
    cache_list_greater_k.push_front(item); //放在大于k的链表前面
    auto update_key = std::make_pair(item.sst_id, item.block_id);
    cache_map_[update_key] = cache_list_greater_k.begin();
  } else if (it->access_count > k_) {
    //访问数大于k，就将其放置在当前链表的头部
    cache_list_greater_k.splice(cache_list_greater_k.begin(),
                                cache_list_greater_k, it);
  }
}
} // namespace tiny_lsm