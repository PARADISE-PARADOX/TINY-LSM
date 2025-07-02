#pragma once

#include "../iterator/iterator.h"
#include "../skiplist/skiplist.h"
#include <cstddef>
#include <functional>
#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace toni_lsm {

class BlockCache; // 块缓存类
class SST; //SST(排序字符串表)类
class SSTBuilder; // SST构建器类
class TranContext; // 事务上下文类

class MemTable {
  friend class TranContext;
  friend class HeapIterator;

private:
  void put_(const std::string &key, const std::string &value,
            uint64_t tranc_id); //键值对插入操作

  SkipListIterator get_(const std::string &key, uint64_t tranc_id); //查找

  SkipListIterator cur_get_(const std::string &key, uint64_t tranc_id); //在活跃表中查询

  SkipListIterator frozen_get_(const std::string &key, uint64_t tranc_id); //在冻结表中查询

  void remove_(const std::string &key, uint64_t tranc_id); //删除

  void frozen_cur_table_(); // _ 表示不需要锁的版本

public:
  MemTable();
  ~MemTable();

  void put(const std::string &key, const std::string &value, uint64_t tranc_id);// 线程安全的插入键值对操作
  void put_batch(const std::vector<std::pair<std::string, std::string>> &kvs,
                 uint64_t tranc_id); // 批量插入键值对

  SkipListIterator get(const std::string &key, uint64_t tranc_id); // 线程安全的查找操作
  std::vector<
      std::pair<std::string, std::optional<std::pair<std::string, uint64_t>>>>
  get_batch(const std::vector<std::string> &keys, uint64_t tranc_id); // 批量查找操作
  void remove(const std::string &key, uint64_t tranc_id);
  void remove_batch(const std::vector<std::string> &keys, uint64_t tranc_id); // 批量删除操作

  void clear();
  std::shared_ptr<SST> flush_last(SSTBuilder &builder, std::string &sst_path,
                                  size_t sst_id,
                                  std::shared_ptr<BlockCache> block_cache);// 将最早的一个SST进行持久化, 形成一个Level 0的SST,
  void frozen_cur_table(); // 冻结当前活跃表(线程安全)
  size_t get_cur_size(); // 获取当前活跃表大小
  size_t get_frozen_size(); // 获取所有冻结表总大小
  size_t get_total_size();  // 获取总大小(活跃+冻结)
  HeapIterator begin(uint64_t tranc_id);  // 获取起始迭代器(包含所有表的合并视图)
  HeapIterator iters_preffix(const std::string &preffix, uint64_t tranc_id);

  std::optional<std::pair<HeapIterator, HeapIterator>>
  iters_monotony_predicate(uint64_t tranc_id,
                           std::function<int(const std::string &)> predicate); // 获取满足单调性谓词的迭代器范围

  HeapIterator end(); // 获取结束迭代器

private:
  std::shared_ptr<SkipList> current_table;
  std::list<std::shared_ptr<SkipList>> frozen_tables;
  size_t frozen_bytes;
  std::shared_mutex frozen_mtx; // 冻结表的锁
  std::shared_mutex cur_mtx;    // 活跃表的锁
};
} // namespace toni_lsm