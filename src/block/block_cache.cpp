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
  std::lock_guard<std::mutex> lock(mutex_);

  //请求数加一
  total_requests_++;

  // 哈希表的键是（sst_id, block_id）
  std::pair<int, int> umap_key = std::make_pair(sst_id, block_id);

  //在缓存中寻找
  auto iter = cache_map_.find(umap_key);

  // 缓存中存在时
  if(iter!=cache_map_.end()){
    CacheItem
  }

}

void BlockCache::put(int sst_id, int block_id, std::shared_ptr<Block> block) {
  // TODO: Lab 4.8 插入一个 Block
}

double BlockCache::hit_rate() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return total_requests_ == 0
             ? 0.0
             : static_cast<double>(hit_requests_) / total_requests_;
}

void BlockCache::update_access_count(std::list<CacheItem>::iterator it) {
  // TODO: Lab 4.8 更新统计信息
}
} // namespace tiny_lsm