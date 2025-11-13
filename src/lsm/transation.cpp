#include "../../include/lsm/engine.h"
#include "../../include/lsm/transaction.h"
#include "../../include/utils/files.h"
#include "spdlog/spdlog.h"
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace tiny_lsm {

inline std::string isolation_level_to_string(const IsolationLevel &level) {
  switch (level) {
  case IsolationLevel::READ_UNCOMMITTED:
    return "READ_UNCOMMITTED";
  case IsolationLevel::READ_COMMITTED:
    return "READ_COMMITTED";
  case IsolationLevel::REPEATABLE_READ:
    return "REPEATABLE_READ";
  case IsolationLevel::SERIALIZABLE:
    return "SERIALIZABLE";
  default:
    return "UNKNOWN";
  }
}

// *********************** TranContext ***********************
TranContext::TranContext(uint64_t tranc_id, std::shared_ptr<LSMEngine> engine,
                         std::shared_ptr<TranManager> tranManager,
                         const enum IsolationLevel &isolation_level)
    : tranc_id_(tranc_id), engine_(std::move(engine)),
      tranManager_(std::move(tranManager)), isolation_level_(isolation_level) {

  // TODO: Lab 5.2 构造函数初始化
  // 构造时记录下操作，方便后续WAL日志记录
  operations.emplace_back(Record::createRecord(tranc_id));
}

void TranContext::put(const std::string &key, const std::string &value) {
  // TODO: Lab 5.2 put 实现

  spdlog::trace("LSM--"
                "lsm_iters_monotony_predicate: Starting query for tranc_id={}",
                this->tranc_id_);

  // 获取隔离等级
  auto isolation_level = get_isolation_level();
  // 记录事务操作记录
  operations.emplace_back(Record::putRecord(this->tranc_id_, key, value));

  if (isolation_level == IsolationLevel::READ_UNCOMMITTED) {
    // 隔离级别是读未提交，直接写入

    // 写入前需要将先前的内容先放在记录中用于回滚
    auto prev_record = engine_->get(key, 0);
    rollback_map_[key] = prev_record;

    engine_->put(key, value, tranc_id_);
    spdlog::trace(
        "TranContext--READ_UNCOMMITTED: put({}, {}) applied to memtable", key,
        value);
    return;
  }

  // 其余隔离级别保存到 temp_map_ ，提交后才会生效，现在属于暂存状态
  temp_map_[key] = value;
  spdlog::trace("TranContext--{}: put({}, {}) stored in temp map",
                isolation_level_to_string(isolation_level_), key, value);
}

void TranContext::remove(const std::string &key) {
  // TODO: Lab 5.2 remove 实现
  spdlog::trace("TranContext--remove({}) called, tranc_id={}", key, tranc_id_);

  auto isolation_level = get_isolation_level();

  // 所有隔离级别都需要先写入 operations 中
  operations.emplace_back(Record::deleteRecord(this->tranc_id_, key));

  if (isolation_level == IsolationLevel::READ_UNCOMMITTED) {
    // 1 如果隔离级别是 READ_UNCOMMITTED, 直接写入 memtable
    // 先查询以前的记录, 因为回滚时可能需要
    auto prev_record = engine_->get(key, 0);
    rollback_map_[key] = prev_record;
    engine_->remove(key, tranc_id_);

    spdlog::trace(
        "TranContext--READ_UNCOMMITTED: remove({}) applied to memtable", key);

    return;
  }

  // 2 其他隔离级别需要 暂存到 temp_map_ 中, 统一提交后才在数据库中生效
  temp_map_[key] = "";
  spdlog::trace("TranContext--{}: remove({}) stored in temp map",
                isolation_level_to_string(isolation_level_), key);
}

std::optional<std::string> TranContext::get(const std::string &key) {
  // TODO: Lab 5.2 get 实现

  spdlog::trace("TranContext--get({}) called, tranc_id={}", key, tranc_id_);
  auto isolation_level = get_isolation_level();

  // 所有内容先在临时缓存temp_map_查询
  if (temp_map_.find(key) != temp_map_.end()) {
    spdlog::trace("TranContext--{}: get({}) found in temp map",
                  isolation_level_to_string(isolation_level), key);
    return temp_map_[key];
  }

  // 临时缓存中没有时，去engine查询
  std::optional<std::pair<std::string, uint64_t>> query;
  if (isolation_level == IsolationLevel::READ_UNCOMMITTED) {
    // 读未提交不需要判断事务id，直接获取
    query = engine_->get(key, 0);
  } else if (isolation_level == IsolationLevel::READ_COMMITTED) {
    // 读已提交时，使用事务id进行判断
    query = engine_->get(key, this->tranc_id_);
  } else {
    if (read_map_.find(key) != read_map_.end()) {
      query =
          read_map_[key]; // read_map_保存首次读取的内容，这样就可以防止幻读。
    } else {
      query = engine_->get(key, this->tranc_id_);
      read_map_[key] = query;
    }
  }

  if (query.has_value()) {
    spdlog::trace("TranContext--{}: get({}) returned value={}",
                  isolation_level_to_string(isolation_level), key,
                  query->first);
  } else {
    spdlog::trace("TranContext--{}: get({}) returned no value",
                  isolation_level_to_string(isolation_level), key);
  }
  return query.has_value() ? std::make_optional(query->first) : std::nullopt;
}

bool TranContext::commit(bool test_fail) {
  // TODO: Lab 5.2 commit 实现
  spdlog::info("TranContext--commit(): Starting commit for transaction ID={}",
               tranc_id_);

  auto isolation_level = get_isolation_level();

  if (isolation_level == IsolationLevel::READ_UNCOMMITTED) {
    //读未提交时需要随着单词的操作更新，不需要统一更新
    operations.emplace_back(Record::commitRecord(this->tranc_id_));

    // 记录后加入wal中
    auto wal_success = tranManager_->write_to_wal(operations);

    if (!wal_success) {
      spdlog::error(
          "TranContext--commit(): Failed to write WAL for transaction ID={}",
          tranc_id_);

      // wal日志写入失败，抛出错误
      throw std::runtime_error("Writ into WAL failed");
    }

    engine_->memtable.put_("", "", tranc_id_); // 结束标志
    isCommited = true;

    // 更新最大的事务id
    tranManager_->update_max_finished_tranc_id(tranc_id_);

    spdlog::info(
        "TranContext--commit(): Transaction ID={} committed successfully",
        tranc_id_);

    return true;
  }

  MemTable &memtable = engine_->memtable;
  std::unique_lock<std::shared_mutex> wlock1(memtable.frozen_mtx);
  std::unique_lock<std::shared_mutex> wlock2(memtable.cur_mtx);

  if (isolation_level == IsolationLevel::REPEATABLE_READ ||
      isolation_level == IsolationLevel::SERIALIZABLE) {

    // 读不可重复读和串行的情况下，需要检测是否冲突

    // SST的锁
    std::shared_lock<std::shared_mutex> rlock3(engine_->ssts_mtx);

    // 获取暂存的数据，查看数据是否在memtable和SST中已经存在
    for (auto &[k, v] : temp_map_) {
      auto res = memtable.get_(k, 0);

      if (res.is_valid() && res.get_tranc_id() > tranc_id_) {
        // memtable中存在这个键，且事务id大于暂存事务的id，表示写入内存表的事务更晚创建的，有更高的优先级，所以需要保留原本的，暂存的视为冲突
        isAborted = true;
        spdlog::warn("TranContext--commit(): Conflict detected on key={}, "
                     "aborting transaction ID={}",
                     k, tranc_id_);

        return false;
      } else {
        // 需要判断事务id是否大于刷入的最大事务id，小于的话就不需要执行后续操作
        if (tranManager_->get_max_flushed_tranc_id() <= tranc_id_) {
          continue;
        } else {
          // 检查sst是否冲突
          auto res = engine_->sst_get_(k, 0);
          if (res.has_value()) {
            auto [v, id] = res.value();
            if (id > tranc_id_) {
              // 存在相同的 key , 且其 tranc_id 大于暂存的
              // tranc_id，表示发生了冲突
              isAborted = true;
              spdlog::warn("TranContext--commit(): SST conflict on key={}, "
                           "aborting transaction ID={}",
                           k, tranc_id_);
              return false;
            }
          }
        }
      }
    }
  }

  // 没有冲突时，允许正常写入
  operations.emplace_back(Record::commitRecord(this->tranc_id_));

  auto wal_success = tranManager_->write_to_wal(operations);

  if (!wal_success) {
    spdlog::error(
        "TranContext--commit(): Failed to write WAL for transaction ID={}",
        tranc_id_);

    throw std::runtime_error("write to wal failed");
  }

  // 将暂存数据应用到数据库
  if (!test_fail) {
    // 这里是手动调用 memtable 的无锁版本的 put_, 因为之前手动加了写锁
    for (auto &[k, v] : temp_map_) {
      memtable.put_(k, v, tranc_id_);
    }
  }

  isCommited = true;
  tranManager_->update_max_finished_tranc_id(tranc_id_);

  spdlog::info(
      "TranContext--commit(): Transaction ID={} committed successfully",
      tranc_id_);

  return true;
}

bool TranContext::abort() {
  // TODO: Lab 5.2 abort 实现
  auto isolation_level = get_isolation_level();
  spdlog::info("TranContext--abort(): Aborting transaction ID={}", tranc_id_);

  if (isolation_level == IsolationLevel::READ_UNCOMMITTED) {
    // 读未提交时，手动回滚
    for (auto &[k, res] : rollback_map_) {
      if (res.has_value()) {
        engine_->put(k, res.value().first, res.value().second);
      } else {
        // 之前本就不存在, 需要移除当前事务的新增操作
        engine_->remove(k, tranc_id_);
      }
    }

    isAborted = true;
    spdlog::info("TranContext--abort(): Transaction ID={} aborted", tranc_id_);
    return true;
  }

  isAborted = true;
  return true;
}

enum IsolationLevel TranContext::get_isolation_level() {
  return isolation_level_;
}

// *********************** TranManager ***********************
TranManager::TranManager(std::string data_dir) : data_dir_(data_dir) {
  auto file_path = get_tranc_id_file_path();

  // TODO: Lab 5.2 初始化时读取持久化的事务状态信息

  if (!std::filesystem::exists(file_path)) {
    tranc_id_file_ = FileObj::open(file_path, true);
  } else {
    tranc_id_file_ = FileObj::open(file_path, false);
    read_tranc_id_file();
  }
}

void TranManager::init_new_wal() {
  // TODO: Lab 5.x 初始化 wal
}

void TranManager::set_engine(std::shared_ptr<LSMEngine> engine) {
  engine_ = std::move(engine);
}

TranManager::~TranManager() { write_tranc_id_file(); }

void TranManager::write_tranc_id_file() {
  // TODO: Lab 5.2 持久化事务状态信息
  // 共4个8字节的整型id记录需要持久化
  // std::atomic<uint64_t> nextTransactionId_;
  // std::atomic<uint64_t> max_flushed_tranc_id_;
  // std::atomic<uint64_t> max_finished_tranc_id_;

  std::vector<uint8_t> now_id(3 * sizeof(uint64_t), 0);
  uint64_t nextTransactionId = nextTransactionId_.load();
  uint64_t max_flushed_tranc_id = max_flushed_tranc_id_.load();
  uint64_t max_finished_tranc_id = max_finished_tranc_id_.load();

  auto ptr = now_id.data();
  memcpy(ptr, &nextTransactionId, sizeof(uint64_t));
  ptr += sizeof(uint64_t);

  memcpy(ptr,&max_flushed_tranc_id, sizeof(uint64_t));
  ptr += sizeof(uint64_t);

  memcpy(ptr,&max_finished_tranc_id, sizeof(uint64_t));


  // 写入磁盘，实现持久化
  tranc_id_file_.write(0, now_id);
  tranc_id_file_.sync();


}

void TranManager::read_tranc_id_file() {
  // TODO: Lab 5.2 读取持久化的事务状态信息
  nextTransactionId_ = tranc_id_file_.read_uint64(0);
  max_flushed_tranc_id_ = tranc_id_file_.read_uint64(sizeof(uint64_t));
  max_finished_tranc_id_ = tranc_id_file_.read_uint64(sizeof(uint64_t) * 2);
}

void TranManager::update_max_finished_tranc_id(uint64_t tranc_id) {
  // TODO: Lab 5.2 更新持久化的事务状态信息
  uint64_t expected = max_finished_tranc_id_.load(std::memory_order_relaxed);
  while (tranc_id > expected) {
    if (max_finished_tranc_id_.compare_exchange_weak(
            expected, tranc_id, std::memory_order_acq_rel,
            std::memory_order_relaxed)) {
      break;
    }
  }
}

void TranManager::update_max_flushed_tranc_id(uint64_t tranc_id) {
  // TODO: Lab 5.2 更新持久化的事务状态信息
  uint64_t expected = max_flushed_tranc_id_.load(std::memory_order_relaxed);
  while (tranc_id > expected) {
    if (max_flushed_tranc_id_.compare_exchange_weak(
            expected, tranc_id, std::memory_order_acq_rel,
            std::memory_order_relaxed)) {
      break;
    }
  }
  write_tranc_id_file();
}

uint64_t TranManager::getNextTransactionId() {
  return nextTransactionId_.fetch_add(1, std::memory_order_relaxed);
}

uint64_t TranManager::get_max_flushed_tranc_id() {
  return max_flushed_tranc_id_.load();
}

uint64_t TranManager::get_max_finished_tranc_id_() {
  return max_finished_tranc_id_.load();
}

std::shared_ptr<TranContext>
TranManager::new_tranc(const IsolationLevel &isolation_level) {
  // TODO: Lab 5.2 事务上下文分配

  spdlog::debug("TranManager--new_tranc(): Creating new transaction with "
                "isolation level={}",
                static_cast<int>(isolation_level));

  // 获取锁
  std::unique_lock<std::mutex> lock(mutex_);

  auto tranc_id = getNextTransactionId();

  // 获取事务id对应的上下文
  activeTrans_[tranc_id] = std::make_shared<TranContext>(
      tranc_id, engine_, shared_from_this(), isolation_level);

  spdlog::debug("TranManager--new_tranc(): Created transaction ID={} with "
                "isolation level={}",
                tranc_id, static_cast<int>(isolation_level));

  return activeTrans_[tranc_id];
}
std::string TranManager::get_tranc_id_file_path() {
  if (data_dir_.empty()) {
    data_dir_ = "./";
  }
  return data_dir_ + "/tranc_id";
}

std::map<uint64_t, std::vector<Record>> TranManager::check_recover() {
  // TODO: Lab 5.5
  return {};
}

bool TranManager::write_to_wal(const std::vector<Record> &records) {
  // TODO: Lab 5.4

  return true;
}

// void TranManager::flusher() {
//   while (flush_thread_running_.load()) {
//     std::this_thread::sleep_for(std::chrono::seconds(1));
//     write_tranc_id_file();
//   }
// }
} // namespace tiny_lsm