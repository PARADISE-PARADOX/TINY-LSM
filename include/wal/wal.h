// include/wal/wal.h

#pragma once

#include "../utils/files.h"
#include "record.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tiny_lsm {

class WAL {
public:
  WAL(const std::string &log_dir, size_t buffer_size,
      uint64_t max_finished_tranc_id, uint64_t clean_interval,
      uint64_t file_size_limit);
  ~WAL();

  // 恢复WAL文件
  static std::map<uint64_t, std::vector<Record>>
  recover(const std::string &log_dir, uint64_t max_finished_tranc_id);

  // 将记录添加到缓冲区
  void log(const std::vector<Record> &records, bool force_flush = false);

  // 写入 WAL 文件
  void flush();

  void set_checkpoint_tranc_id(uint64_t checkpoint_tranc_id);


private:
  void cleaner();
  void cleanWALFile();
  void reset_file();


protected:
  std::string active_log_path_; // 当前写入的WAL文件路径
  FileObj log_file_; // 当前写入的WAL文件对象
  size_t file_size_limit_; // WAL文件的大小限制
  std::mutex mutex_; 
  std::vector<Record> log_buffer_; // 缓冲区
  size_t buffer_size_; // 缓冲区阈值
  std::thread cleaner_thread_; // 清理线程
  uint64_t checkpoint_tranc_id_; // 检查点的id
  std::atomic<bool> stop_cleaner_; // 作为线程停止标志，通知清理线程何时应该退出
  uint64_t clean_interval_; // 清理线程的工作间隔时间
};
} // namespace tiny_lsm