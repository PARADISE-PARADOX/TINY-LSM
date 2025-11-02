#pragma once

#include "../utils/files.h"
#include "../wal/wal.h"
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace tiny_lsm {

enum class IsolationLevel {
  READ_UNCOMMITTED,
  READ_COMMITTED,
  REPEATABLE_READ,
  SERIALIZABLE
};

inline std::string isolation_level_to_string(const IsolationLevel &level);

class LSMEngine;
class TranManager;

class TranContext {
  friend class TranManager;

public:
  TranContext(uint64_t tranc_id, std::shared_ptr<LSMEngine> engine,
              std::shared_ptr<TranManager> tranManager,
              const enum IsolationLevel &isolation_level);
  void put(const std::string &key, const std::string &value);
  void remove(const std::string &key);
  std::optional<std::string> get(const std::string &key);

  // ! test_fail = true 是测试中手动触发的崩溃
  bool commit(bool test_fail = false);
  bool abort();
  enum IsolationLevel get_isolation_level();

public:
  std::shared_ptr<LSMEngine> engine_;
  std::shared_ptr<TranManager> tranManager_;
  uint64_t tranc_id_;
  std::vector<Record> operations; // 事务操作记录，转为WAL日志
  std::unordered_map<std::string, std::string> temp_map_; // 事务上下文中的临时数据
  bool isCommited = false;
  bool isAborted = false;
  enum IsolationLevel isolation_level_;

private:
  std::unordered_map<std::string,
                     std::optional<std::pair<std::string, uint64_t>>>
      read_map_; // 首次读取后会存储其内容，这样重复读的时候不会出现幻读的情况
  std::unordered_map<std::string,
                     std::optional<std::pair<std::string, uint64_t>>>
      rollback_map_; // 事务回滚记录, 主要用于事务的回滚
};

class TranManager : public std::enable_shared_from_this<TranManager> {
public:
  TranManager(std::string data_dir);
  ~TranManager();
  void init_new_wal();
  void set_engine(std::shared_ptr<LSMEngine> engine);
  std::shared_ptr<TranContext> new_tranc(const IsolationLevel &isolation_level);

  uint64_t getNextTransactionId();
  uint64_t get_max_flushed_tranc_id();
  uint64_t get_max_finished_tranc_id_();

  void update_max_finished_tranc_id(uint64_t tranc_id);
  void update_max_flushed_tranc_id(uint64_t tranc_id);

  bool write_to_wal(const std::vector<Record> &records);

  std::map<uint64_t, std::vector<Record>> check_recover();

  std::string get_tranc_id_file_path();
  void write_tranc_id_file();
  void read_tranc_id_file();
  // void flusher();

private:
  mutable std::mutex mutex_;
  std::shared_ptr<LSMEngine> engine_;
  std::shared_ptr<WAL> wal;
  std::string data_dir_;
  // std::atomic<bool> flush_thread_running_ = true;
  std::atomic<uint64_t> nextTransactionId_ = 1;
  std::atomic<uint64_t> max_flushed_tranc_id_ = 0;
  std::atomic<uint64_t> max_finished_tranc_id_ = 0;
  std::map<uint64_t, std::shared_ptr<TranContext>> activeTrans_;
  FileObj tranc_id_file_;
};

} // namespace tiny_lsm