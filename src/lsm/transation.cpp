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
      query = read_map_[key];
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
  return true;
}

bool TranContext::abort() {
  // TODO: Lab 5.2 abort 实现
  return true;
}

enum IsolationLevel TranContext::get_isolation_level() {
  return isolation_level_;
}

// *********************** TranManager ***********************
TranManager::TranManager(std::string data_dir) : data_dir_(data_dir) {
  auto file_path = get_tranc_id_file_path();

  // TODO: Lab 5.2 初始化时读取持久化的事务状态信息
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
}

void TranManager::read_tranc_id_file() {
  // TODO: Lab 5.2 读取持久化的事务状态信息
}

void TranManager::update_max_finished_tranc_id(uint64_t tranc_id) {
  // TODO: Lab 5.2 更新持久化的事务状态信息
}

void TranManager::update_max_flushed_tranc_id(uint64_t tranc_id) {
  // TODO: Lab 5.2 更新持久化的事务状态信息
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

  //获取事务id对应的上下文
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