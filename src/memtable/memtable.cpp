#include "../../include/memtable/memtable.h"
#include "../../include/config/config.h"
#include "../../include/consts.h"
#include "../../include/iterator/iterator.h"
#include "../../include/skiplist/skiplist.h"
#include "../../include/sst/sst.h"
#include "spdlog/spdlog.h"
#include <algorithm>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <sys/types.h>
#include <utility>
#include <vector>

namespace toni_lsm {

class BlockCache;

// MemTable implementation using PIMPL idiom
MemTable::MemTable() : frozen_bytes(0) {
  current_table = std::make_shared<SkipList>();
}
MemTable::~MemTable() = default;

void MemTable::put_(const std::string &key, const std::string &value,
                    uint64_t tranc_id) {
  // TODO: Lab2.1 无锁版本的 put
  current_table->put(key,value,tranc_id);

}

void MemTable::put(const std::string &key, const std::string &value,
                   uint64_t tranc_id) {
  // TODO: Lab2.1 有锁版本的 put
  spdlog::trace("Memtable is put({},{},{}) called",key,value,tranc_id);\

  std::unique_lock<std::shared_mutex> cur_lock(cur_mtx);
  put_(key,value,tranc_id);

  // 当前的memtable大小比设定的最大容量大时，冻结该表
  if(current_table->get_size() > TomlConfig::getInstance().getLsmPerMemSizeLimit()){
    //获取冻结表的写锁
    std::unique_lock<std::shared_mutex> frozen_lock(frozen_mtx);
    frozen_cur_table_(); //冻结当前的表
    spdlog::debug("Memtable is frozen");
  }
  
}

void MemTable::put_batch(
    const std::vector<std::pair<std::string, std::string>> &kvs,
    uint64_t tranc_id) {
  // TODO: Lab2.1 有锁版本的 put_batch
  // ? tranc_id 参数可暂时忽略其逻辑判断, 直接插入即可
  spdlog::trace("Memtable is put_batch with {} keys",kvs.size());

  std::unique_lock<std::shared_mutex> cur_lock(cur_mtx);
  for(auto &[k,v] : kvs){
    put_(k,v,tranc_id);
  }
  if(current_table->get_size() > TomlConfig::getInstance().getLsmPerMemSizeLimit()){
    //获取冻结表的写锁
    std::unique_lock<std::shared_mutex> frozen_lock(frozen_mtx);
    frozen_cur_table_(); //冻结当前的表
    spdlog::debug("Memtable is frozen with put_batch");
  }
}


SkipListIterator MemTable::cur_get_(const std::string &key, uint64_t tranc_id) {
  // 检查当前活跃的memtable
  // TODO: Lab2.1 从活跃跳表中查询
  auto res = current_table->get(key,tranc_id);
  if(res.is_valid()){
    return res;
  }

  //没找到返回空
  return SkipListIterator{};
}

SkipListIterator MemTable::frozen_get_(const std::string &key,
                                       uint64_t tranc_id) {
  // TODO: Lab2.1 从冻结跳表中查询
  // ? 你需要尤其注意跳表的遍历顺序
  // ? tranc_id 参数可暂时忽略, 直接传递参数即可
  for(const auto& fro_table:frozen_tables){
    auto res = fro_table->get(key,tranc_id);
    if(res.is_valid()){
      return res;
    }
  }
  return SkipListIterator{};
}

SkipListIterator MemTable::get(const std::string &key, uint64_t tranc_id) {
  // TODO: Lab2.1 查询, 建议复用 cur_get_ 和 frozen_get_
  // ? 注意并发控制
  // 先从活跃表中查找，因为是读取，所以使用shared_lock共享模式
  std::unique_lock<std::shared_mutex> shared_cur_lock(cur_mtx);
  auto cur_res = cur_get_(key,tranc_id);
  if(cur_res.is_valid()){
    return cur_res;
  }

  shared_cur_lock.unlock();

  //在从冻结表中查询
  std::shared_lock<std::shared_mutex> shared_forzen_lock(frozen_mtx);
  auto frozen_res = frozen_get_(key, tranc_id);
  if(frozen_res.is_valid()){
    return frozen_res;
  }

  spdlog::trace("MemTable key {} is not found", key);
  return SkipListIterator{};
}

SkipListIterator MemTable::get_(const std::string &key, uint64_t tranc_id) {
  // TODO: Lab2.1 查询, 无锁版本
  auto cur_res = cur_get_(key,tranc_id);
  if(cur_res.is_valid()){
    return cur_res;
  }

  //在从冻结表中查询
  auto frozen_res = frozen_get_(key, tranc_id);
  if(frozen_res.is_valid()){
    return frozen_res;
  }

  spdlog::trace("MemTable key {} is not found without lock", key);
  return SkipListIterator{};
  
}

std::vector<
    std::pair<std::string, std::optional<std::pair<std::string, uint64_t>>>>
MemTable::get_batch(const std::vector<std::string> &keys, uint64_t tranc_id) {
  spdlog::trace("MemTable--get_batch with {} keys", keys.size());

  std::vector<
      std::pair<std::string, std::optional<std::pair<std::string, uint64_t>>>>
      results;
  results.reserve(keys.size());

  // 1. 先获取活跃表的锁
  std::shared_lock<std::shared_mutex> slock1(cur_mtx);
  for (size_t idx = 0; idx < keys.size(); idx++) {
    auto key = keys[idx];
    auto cur_res = cur_get_(key, tranc_id);
    if (cur_res.is_valid()) {
      // 值存在且不为空
      // ! 此时value可能为空, 需要返回时置为 nullopt
      // ! 这里允许value为空主要是为了与"没有找到"区分开来
      results.emplace_back(
          key, std::make_pair(cur_res.get_value(), cur_res.get_tranc_id()));
    } else {
      // 如果活跃表中未找到，先占位
      results.emplace_back(key, std::nullopt);
    }
  }

  // 2. 如果某些键在活跃表中未找到，还需要查找冻结表
  if (!std::any_of(results.begin(), results.end(), [](const auto &result) {
        return !result.second.has_value();
      })) {
    // ! 最后, 需要把 value 为空的键值对标记为 nullopt
    for (auto &[key, value] : results) {
      if (!value.has_value()) {
        value = std::nullopt;
      }
    }
    return results;
  }

  slock1.unlock();                                        // 释放活跃表的锁
  std::shared_lock<std::shared_mutex> slock2(frozen_mtx); // 获取冻结表的锁
  for (size_t idx = 0; idx < keys.size(); idx++) {
    if (results[idx].second.has_value()) {
      continue; // 如果在活跃表中已经找到，则跳过
    }
    auto key = keys[idx];
    auto frozen_result = frozen_get_(key, tranc_id);
    if (frozen_result.is_valid()) {
      // 值存在且不为空
      results[idx] =
          std::make_pair(key, std::make_pair(frozen_result.get_value(),
                                             frozen_result.get_tranc_id()));
    } else {
      results[idx] = std::make_pair(key, std::nullopt);
    }
  }

  // 最后, 需要把 value 为空的键值对标记为 nullopt
  for (auto &[key, value] : results) {
    if (!value.has_value()) {
      value = std::nullopt;
    }
  }

  return results;
}

void MemTable::remove_(const std::string &key, uint64_t tranc_id) {
  // TODO Lab2.1 无锁版本的remove
  current_table->put(key, "", tranc_id);
}

void MemTable::remove(const std::string &key, uint64_t tranc_id) {
  // TODO Lab2.1 有锁版本的remove
  std::unique_lock<std::shared_mutex> cur_lock(cur_mtx);
  remove_(key,tranc_id);

  //因为是放入空的值，所以要判断当前表的大小
  if (current_table->get_size() >
      TomlConfig::getInstance().getLsmPerMemSizeLimit()) {
    // 冻结当前表还需要获取frozen_mtx的写锁
    std::unique_lock<std::shared_mutex> frozen_lock(frozen_mtx);
    frozen_cur_table_();
    spdlog::debug("MemTable--Current table size exceeded limit after remove. "
                  "Frozen and created new table.");
  }
}

void MemTable::remove_batch(const std::vector<std::string> &keys,
                            uint64_t tranc_id) {
  // TODO Lab2.1 有锁版本的remove_batch

  std::unique_lock<std::shared_mutex> cur_lock(cur_mtx);
  for(auto& k:keys){
    remove_(k,tranc_id);
  }

  if (current_table->get_size() >
      TomlConfig::getInstance().getLsmPerMemSizeLimit()) {
    // 冻结当前表还需要获取frozen_mtx的写锁
    std::unique_lock<std::shared_mutex> frozen_lock(frozen_mtx);
    frozen_cur_table_();
    
  }

}

void MemTable::clear() {
  spdlog::info("MemTable--clear(): Clearing all tables");

  std::unique_lock<std::shared_mutex> lock1(cur_mtx);
  std::unique_lock<std::shared_mutex> lock2(frozen_mtx);
  frozen_tables.clear();
  current_table->clear();
}

// 将最老的 memtable 写入 SST, 并返回控制类
std::shared_ptr<SST>
MemTable::flush_last(SSTBuilder &builder, std::string &sst_path, size_t sst_id,
                     std::shared_ptr<BlockCache> block_cache) {
  spdlog::debug("MemTable--flush_last(): Starting to flush memtable to SST{}",
                sst_id);

  // 由于 flush 后需要移除最老的 memtable, 因此需要加写锁
  std::unique_lock<std::shared_mutex> lock(frozen_mtx);

  uint64_t max_tranc_id = 0;
  uint64_t min_tranc_id = UINT64_MAX;

  if (frozen_tables.empty()) {
    // 如果当前表为空，直接返回nullptr
    if (current_table->get_size() == 0) {
      spdlog::debug(
          "MemTable--flush_last(): Current table is empty, returning null");

      return nullptr;
    }
    // 将当前表加入到frozen_tables头部
    frozen_tables.push_front(current_table);
    frozen_bytes += current_table->get_size();
    // 创建新的空表作为当前表
    current_table = std::make_shared<SkipList>();
  }

  // 将最老的 memtable 写入 SST
  std::shared_ptr<SkipList> table = frozen_tables.back();
  frozen_tables.pop_back();
  frozen_bytes -= table->get_size();

  std::vector<std::tuple<std::string, std::string, uint64_t>> flush_data =
      table->flush();
  for (auto &[k, v, t] : flush_data) {
    max_tranc_id = std::max(t, max_tranc_id);
    min_tranc_id = std::min(t, min_tranc_id);
    builder.add(k, v, t);
  }
  auto sst = builder.build(sst_id, sst_path, block_cache);

  spdlog::info("MemTable--flush_last(): SST{} built successfully at '{}'",
               sst_id, sst_path);

  return sst;
}

void MemTable::frozen_cur_table_() {
  // TODO: 冻结活跃表
  spdlog::trace("frozen current Memtable");
  frozen_bytes += current_table->get_size();
  frozen_tables.push_front(std::move(current_table)); //使用move将当前表传入到冻结表头中，顺便让当前表为空
  current_table = std::make_shared<SkipList>();
}

void MemTable::frozen_cur_table() {
  // TODO: 冻结活跃表, 有锁版本
  spdlog::trace("frozen current Memtable with lock");
  std::unique_lock<std::shared_mutex> cur_lock(cur_mtx);
  std::unique_lock<std::shared_mutex> frozen_lock(frozen_mtx);
  frozen_cur_table_();
}

size_t MemTable::get_cur_size() {
  std::shared_lock<std::shared_mutex> slock(cur_mtx);
  return current_table->get_size();
}

size_t MemTable::get_frozen_size() {
  std::shared_lock<std::shared_mutex> slock(frozen_mtx);
  return frozen_bytes;
}

size_t MemTable::get_total_size() {
  std::shared_lock<std::shared_mutex> slock1(cur_mtx);
  std::shared_lock<std::shared_mutex> slock2(frozen_mtx);
  return get_frozen_size() + get_cur_size();
}

HeapIterator MemTable::begin(uint64_t tranc_id) {
  // TODO Lab 2.2 MemTable 的迭代器
  std::shared_lock<std::shared_mutex> shared_cur_lock(cur_mtx);
  std::shared_lock<std::shared_mutex> shared_frozen_lock(frozen_mtx);

  std::vector<SearchItem> item_vector;

  //将活跃表current_table中的内容存放到item_vector中
  for(auto it = current_table->begin();it!=current_table->end();++it){
    if(tranc_id != 0 && tranc_id < it.get_tranc_id()){
      //如果事务id小于当前元素的事务id，说明该元素不可见，所以跳过
      continue;
    }
    item_vector.emplace_back(it.get_key(),it.get_value(),0,0,it.get_tranc_id());
  }

  // 将冻结表的值放入item_vector
  int idx = 1;
  for(const auto& table : frozen_tables) {
    for(auto it = table->begin(); it != table->end(); ++it) {
      if(tranc_id != 0 && tranc_id < it.get_tranc_id()) {
        continue;
      }
      item_vector.emplace_back(it.get_key(), it.get_value(), idx, 0, it.get_tranc_id());
    }
    idx++;
  }

  return HeapIterator(item_vector,tranc_id);
}

HeapIterator MemTable::end() {
  // TODO Lab 2.2 MemTable 的迭代器
  std::shared_lock<std::shared_mutex> shared_cur_lock(cur_mtx);
  std::shared_lock<std::shared_mutex> shared_frozen_lock(frozen_mtx);

   std::vector<SearchItem> empty_items;
  return HeapIterator{empty_items,0};
}

HeapIterator MemTable::iters_preffix(const std::string &preffix,
                                     uint64_t tranc_id) {

  // TODO Lab 2.3 MemTable 的前缀迭代器

  return {};
}

std::optional<std::pair<HeapIterator, HeapIterator>>
MemTable::iters_monotony_predicate(
    uint64_t tranc_id, std::function<int(const std::string &)> predicate) {
  // TODO Lab 2.3 MemTable 的谓词查询迭代器起始范围
  return std::nullopt;
}
} // namespace toni_lsm