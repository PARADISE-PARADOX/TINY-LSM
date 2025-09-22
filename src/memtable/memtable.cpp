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

namespace tiny_lsm {

class BlockCache;

// MemTable implementation using PIMPL idiom
MemTable::MemTable() : frozen_bytes(0) {
  current_table = std::make_shared<SkipList>();
}
MemTable::~MemTable() = default;

void MemTable::put_(const std::string &key, const std::string &value,
                    uint64_t tranc_id) {
  // TODO: Lab2.1 无锁版本的 put
  current_table->put(key, value, tranc_id);
}

void MemTable::put(const std::string &key, const std::string &value,
                   uint64_t tranc_id) {
  // TODO: Lab2.1 有锁版本的 put
  spdlog::trace("put ({},{},{})", key, value, tranc_id);

  //为活跃表添加写锁
  std::unique_lock<std::shared_mutex> cur_lock(cur_mtx);

  put_(key, value, tranc_id);

  // 判断活跃表的大小是否超过阈值
  if (current_table->get_size() >
      TomlConfig::getInstance().getLsmPerMemSizeLimit()) {

    //超过阈值的时候，需要将活跃表中的内容冻结并放入冻结表中
    std::unique_lock<std::shared_mutex> fro_lock(frozen_mtx);
    //冻结活跃表
    frozen_cur_table_();
    spdlog::trace("current table size exceeded limit,should forzen this table "
                  "and create new cur_table");
  }
}

void MemTable::put_batch(
    const std::vector<std::pair<std::string, std::string>> &kvs,
    uint64_t tranc_id) {
  // TODO: Lab2.1 有锁版本的 put_batch
  // ? tranc_id 参数可暂时忽略其逻辑判断, 直接插入即可
  spdlog::trace("MemTable put_batch with {} keys", kvs.size());

  // 活跃表加入写锁
  std::unique_lock<std::shared_mutex> cur_lock(cur_mtx);
  for (auto &[k, v] : kvs) {
    put_(k, v, tranc_id);
  }

  // 判断活跃表的大小是否超过阈值
  if (current_table->get_size() >
      TomlConfig::getInstance().getLsmPerMemSizeLimit()) {

    //超过阈值的时候，需要将活跃表中的内容冻结并放入冻结表中
    std::unique_lock<std::shared_mutex> fro_lock(frozen_mtx);
    //冻结活跃表
    frozen_cur_table_();
    spdlog::debug("current table size exceeded limit,should forzen this table "
                  "and create new cur_table");
  }
}

SkipListIterator MemTable::cur_get_(const std::string &key, uint64_t tranc_id) {
  // 检查当前活跃的memtable
  // TODO: Lab2.1 从活跃跳表中查询

  // 调用已有的get方法
  SkipListIterator result = current_table->get(key, tranc_id);

  // 结果存在直接返回，不存在返回空迭代器
  if (result.is_valid()) {
    return result;
  }

  return SkipListIterator{};
}

SkipListIterator MemTable::frozen_get_(const std::string &key,
                                       uint64_t tranc_id) {
  // TODO: Lab2.1 从冻结跳表中查询
  // ? 你需要尤其注意跳表的遍历顺序
  // ? tranc_id 参数可暂时忽略, 直接传递参数即可

  for (auto &f_t : frozen_tables) {
    SkipListIterator result = f_t->get(key, tranc_id);
    if (result.is_valid()) {
      return result;
    }
  }

  return SkipListIterator{};
}

SkipListIterator MemTable::get(const std::string &key, uint64_t tranc_id) {
  // TODO: Lab2.1 查询, 建议复用 cur_get_ 和 frozen_get_
  // ? 注意并发控制

  spdlog::trace("Memtable get {}", key);

  // 为活跃表添加读锁
  std::shared_lock<std::shared_mutex> cur_slock(cur_mtx);
  auto current_result = cur_get_(key, tranc_id);
  if (current_result.is_valid()) {
    return current_result;
  }

  //结束对活跃表的检查后解锁
  cur_slock.unlock();

  //如果上述情况没有找到，为冻结表添加读锁
  std::shared_lock<std::shared_mutex> fro_slock(frozen_mtx);
  auto frozen_result = frozen_get_(key, tranc_id);
  if (frozen_result.is_valid()) {
    return frozen_result;
  }

  spdlog::trace("({}) is not found in Memtable", key);

  return SkipListIterator{};
}

SkipListIterator MemTable::get_(const std::string &key, uint64_t tranc_id) {
  // TODO: Lab2.1 查询, 无锁版本

  auto current_result = cur_get_(key, tranc_id);
  if (current_result.is_valid()) {
    return current_result;
  }

  auto frozen_result = frozen_get_(key, tranc_id);
  if (frozen_result.is_valid()) {
    return frozen_result;
  }

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

  slock1.unlock(); // 释放活跃表的锁
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

  // 查询是否存在这个迭代器
  // auto removed_target = get_(key,tranc_id);
  // if(removed_target.is_valid()){
  //   current_table->put(key, "", tranc_id);
  // } else {
  //   spdlog::debug("({}) cannot removed, because is not found in
  //   current_able",key);
  // }
  current_table->put(key, "", tranc_id);
}

void MemTable::remove(const std::string &key, uint64_t tranc_id) {
  // TODO Lab2.1 有锁版本的remove
  spdlog::trace("remove ({}) from Memtable");

  // remove操作是通过写入空值实现，所以需要添加写锁
  std::unique_lock<std::shared_mutex> remove_cur_lock(cur_mtx);
  remove_(key, tranc_id);

  // 后判断大小是否大于阈值
  if (current_table->get_size() >
      TomlConfig::getInstance().getLsmPerMemSizeLimit()) {
    std::unique_lock<std::shared_mutex> remove_frozen_lock(cur_mtx);
    frozen_cur_table_();
    spdlog::debug("current table size exceeded limit after removed");
  }
}

void MemTable::remove_batch(const std::vector<std::string> &keys,
                            uint64_t tranc_id) {
  // TODO Lab2.1 有锁版本的remove_batch
  std::unique_lock<std::shared_mutex> remove_cur_lock(cur_mtx);

  for (auto &k : keys) {
    remove_(k, tranc_id);
  }

  if (current_table->get_size() >
      TomlConfig::getInstance().getLsmPerMemSizeLimit()) {
    std::unique_lock<std::shared_mutex> remove_frozen_lock(cur_mtx);
    frozen_cur_table_();
    spdlog::debug("current table size exceeded limit after batch removed");
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
  spdlog::trace(
      "MemTable--frozen_cur_table_(): Freezing current table without lock");

  //更新冻结表的大小
  frozen_bytes += current_table->get_size();
  //按照从新倒旧的顺序，将新冻结的表放在原本冻结表的表头
  //通过std::move将管理权转移给冻结表
  frozen_tables.push_front(std::move(current_table));
  //将活跃表置空
  current_table = std::make_shared<SkipList>();
}

void MemTable::frozen_cur_table() {
  // TODO: 冻结活跃表, 有锁版本
  spdlog::trace(
      "MemTable--frozen_cur_table(): Using lock to freezing current table");
  // 活跃表和冻结表一并使用写锁
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
  std::shared_lock<std::shared_mutex> cur_lock(cur_mtx);
  std::shared_lock<std::shared_mutex> frozen_lock(frozen_mtx);

  std::vector<SearchItem> item_vec;

  //遍历活跃表
  for (auto it = current_table->begin(); it != current_table->end(); ++it) {
    //判断事务id是否有效
    if (tranc_id != 0 && it.get_tranc_id() > tranc_id) {
      //这种情况跳过
      continue;
    }

    // id有效时放入数组
    item_vec.emplace_back(it.get_key(), it.get_value(), it.get_tranc_id(), 0,
                          0);
  }

  //开始遍历冻结表
  int table_idx = 1;
  for (auto ft = frozen_tables.begin(); ft != frozen_tables.end(); ft++) {
    auto table = *ft;
    for (auto iter = table->begin(); iter != table->end(); ++iter) {
      if (tranc_id != 0 && iter.get_tranc_id() > tranc_id) {
        continue;
      }
      item_vec.emplace_back(iter.get_key(), iter.get_value(),
                            iter.get_tranc_id(), table_idx, 0);
    }
    table_idx++;
  }

  return HeapIterator(item_vec, tranc_id);
}

HeapIterator MemTable::end() {
  std::shared_lock<std::shared_mutex> cur_lock(cur_mtx);
  std::shared_lock<std::shared_mutex> frozen_lock(frozen_mtx);
  return HeapIterator{};
}

HeapIterator MemTable::iters_preffix(const std::string &preffix,
                                     uint64_t tranc_id) {

  // TODO Lab 2.3 MemTable 的前缀迭代器
  std::shared_lock<std::shared_mutex> cur_lock(cur_mtx);
  std::shared_lock<std::shared_mutex> frozen_lock(frozen_mtx);
  std::vector<SearchItem> item_vec;

  for (auto it = current_table->begin_preffix(preffix);
       it != current_table->end_preffix(preffix); ++it) {

    if (tranc_id != 0 && it.get_tranc_id() > tranc_id) {
      //开启事务且事务id大于当前的id时，这个记录是不可见的
      continue;
    }

    if (!item_vec.empty() && item_vec.back().key_ == it.get_key()) {
      // 因为item_vec数组的元素是按照顺序放入的，所以当前数组的末尾元素的key一定和当前的元素相等或者相近，所以只需要判断这个元素即可
      continue;
    }

    item_vec.emplace_back(it.get_key(), it.get_value(), it.get_tranc_id(), 0,
                          0);
    spdlog::trace("get range of element from current_table");
  }

  int table_idx = 1;
  for (auto ft = frozen_tables.begin(); ft != frozen_tables.end(); ++ft) {
    auto table = *ft;
    for (auto it = table->begin_preffix(preffix);
         it != table->end_preffix(preffix); ++it) {
      if (tranc_id != 0 && it.get_tranc_id() > tranc_id) {
        //开启事务且事务id大于当前的id时，这个记录是不可见的
        continue;
      }

      if (!item_vec.empty() && item_vec.back().key_ == it.get_key()) {
        // 因为item_vec数组的元素是按照顺序放入的，所以当前数组的末尾元素的key一定和当前的元素相等或者相近，所以只需要判断这个元素即可
        continue;
      }

      item_vec.emplace_back(it.get_key(), it.get_value(), it.get_tranc_id(), table_idx,
                            0);

      spdlog::trace("get range of element from frozen_tables");
    }
    table_idx++;
  }

  return HeapIterator(item_vec, tranc_id);
}

std::optional<std::pair<HeapIterator, HeapIterator>>
MemTable::iters_monotony_predicate(
    uint64_t tranc_id, std::function<int(const std::string &)> predicate) {
  // TODO Lab 2.3 MemTable 的谓词查询迭代器起始范围
  std::shared_lock<std::shared_mutex> cur_lock(cur_mtx);
  std::shared_lock<std::shared_mutex> frozen_lock(frozen_mtx);
  std::vector<SearchItem> item_vec;

  //获取活跃表中的谓词查询
  auto cur_result = current_table->iters_monotony_predicate(predicate);

  // 在活跃表中查到了内容时
  if (cur_result.has_value()) {
    auto [begin, end] = cur_result.value();
    for (auto it = begin; it != end; ++it) {
      if (tranc_id != 0 && it.get_tranc_id() > tranc_id) {
        // 如果开启了事务, 比当前事务 id 更大的记录是不可见的
        continue;
      }
      if (!item_vec.empty() && item_vec.back().key_ == it.get_key()) {
        // 如果key相同，则只保留最新的事务修改的记录即可
        // 且这个记录既然已经存在于item_vec中，则其肯定满足了事务的可见性判断
        continue;
      }

      item_vec.emplace_back(it.get_key(), it.get_value(), it.get_tranc_id(), 0,
                            0);

      spdlog::trace("iters_monotony_predicate(): get range from curent table");
    }
  }

  int table_idx = 1;
  for (auto ft = frozen_tables.begin(); ft != frozen_tables.end(); ++ft) {
    auto table = *ft;
    auto frozen_res = table->iters_monotony_predicate(predicate);
    if (frozen_res.has_value()) {
      auto [begin, end] = frozen_res.value();
      for (auto it = begin; it != end; ++it) {
        if (tranc_id != 0 && it.get_tranc_id() > tranc_id) {
          // 如果开启了事务, 比当前事务 id 更大的记录是不可见的
          continue;
        }
        if (!item_vec.empty() && item_vec.back().key_ == it.get_key()) {
          // 如果key相同，则只保留最新的事务修改的记录即可
          // 且这个记录既然已经存在于item_vec中，则其肯定满足了事务的可见性判断
          continue;
        }

        item_vec.emplace_back(it.get_key(), it.get_value(), it.get_tranc_id(),
                              table_idx, 0);                        
      }
      spdlog::trace("iters_monotony_predicate(): get range from frozen table");
    }
    table_idx++;
  }

  if(item_vec.empty()){
     spdlog::trace(
        "MemTable--iters_monotony_predicate(): No matching keys found");
        return std::nullopt;
  }

  return std::make_pair(HeapIterator(item_vec, tranc_id), HeapIterator{});
}
} // namespace tiny_lsm