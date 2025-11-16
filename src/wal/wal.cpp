// src/wal/wal.cpp

#include "../../include/wal/wal.h"
#include <algorithm>
#include <cstdint>
<<<<<<< HEAD
#include <fstream>
#include <iostream>
=======
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
>>>>>>> 75e9754 (recover)
#include <vector>

namespace tiny_lsm {

// 从零开始的初始化流程
WAL::WAL(const std::string &log_dir, size_t buffer_size,
         uint64_t max_finished_tranc_id, uint64_t clean_interval,
         uint64_t file_size_limit) {
  // TODO Lab 5.4 : 实现WAL的初始化流程
<<<<<<< HEAD
=======

  // 1.获取WAL的保存路径
  active_log_path_ = log_dir + "/wal.0";
  // 2.将日志文件保存到路径中
  log_file_ = FileObj::open(active_log_path_, true);

  cleaner_thread_ = std::thread(&WAL::cleaner, this);
>>>>>>> 75e9754 (recover)
}

WAL::~WAL() {
  // TODO Lab 5.4 : 实现WAL的清理流程
<<<<<<< HEAD
=======
  // 首先需要将已经存在的WAL日志刷入
  log({}, true);

  // 线程停止
  {
    std::lock_guard<std::mutex> thread_lock(mutex_);
    stop_cleaner_ = true;
  }

  // 线程可以连接时阻塞连接
  if (cleaner_thread_.joinable()) {
    cleaner_thread_.join();
  }

  // 同步操作确保数据写入磁盘
  log_file_.sync();
>>>>>>> 75e9754 (recover)
}

std::map<uint64_t, std::vector<Record>>
WAL::recover(const std::string &log_dir, uint64_t max_flushed_tranc_id) {
  // TODO: Lab 5.5 检查需要重放的WAL日志
<<<<<<< HEAD
  return {};
}

void WAL::log(const std::vector<Record> &records, bool force_flush) {
  // TODO Lab 5.4 : 实现WAL的写入流程
=======
  std::map<uint64_t, std::vector<Record>> tranc_records{};

  // 如果没有日志存在，返回空
  if (!std::filesystem::exists(log_dir)) {
    return tranc_records;
  }

  // 遍历日志目录下的所有文件
  std::vector<std::string> wal_paths;
  for (const auto &entry : std::filesystem::directory_iterator(log_dir)) {
    if (entry.is_regular_file()) {
      // 读取文件名
      std::string filename = entry.path().filename().string();

      // 文件名不合法，跳过
      if (filename.substr(0, 4) != "wal.") {
        continue;
      }

      wal_paths.push_back(entry.path().string());
    }
  }

  //进行升序排序
  std::sort(wal_paths.begin(), wal_paths.end(),
            [](const std::string &a, const std::string &b) {
              try {
                std::string a_seq = a.substr(a.find_last_of(".") + 1);
                std::string b_seq = b.substr(b.find_last_of(".") + 1);
                // 检查子字符串是否为空
                if (a_seq.empty() || b_seq.empty()) {
                  return a < b;  // fallback to lexicographic comparison
                }
                return std::stoi(a_seq) < std::stoi(b_seq);
              } catch (const std::invalid_argument &) {
                // 如果转换失败，使用字典序比较
                return a < b;
              } catch (const std::out_of_range &) {
                // 如果数字超出范围，使用字典序比较
                return a < b;
              }
            });

  // 获取所有记录
  for (const auto &wal_path : wal_paths) {
    auto wal_file = FileObj::open(wal_path, false);
    auto wal_record_slice = wal_file.read_to_slice(0, wal_file.size());
    auto records = Record::decode(wal_record_slice);
    for (const auto &record : records) {
      if (record.getTrancId() > max_flushed_tranc_id) {
        // 如果记录的 tranc_id 大于 max_finished_tranc_id, 才需要尝试恢复
        tranc_records[record.getTrancId()].push_back(record);
      }
    }
  }

  return tranc_records;
>>>>>>> 75e9754 (recover)
}

// commit 时 强制写入
void WAL::flush() {
  // TODO Lab 5.4 : 强制刷盘
  std::lock_guard<std::mutex> lock(mutex_);
}

// 根据id设置检查点
void WAL::set_checkpoint_tranc_id(uint64_t checkpoint_tranc_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  checkpoint_tranc_id_ = checkpoint_tranc_id;
}

void WAL::log(const std::vector<Record> &records, bool force_flush) {
  // TODO Lab 5.4 : 实现WAL的写入流程
  std::unique_lock<std::mutex> lock(mutex_);

  // 将 records 的所有记录添加到缓冲区
  for (const auto &record : records) {
    log_buffer_.push_back(record);
  }

  //目前缓冲区的大小小于阈值，或者没有强制刷入的时候，不进行写入
  if (log_buffer_.size() < buffer_size_ && force_flush == false) {
    return;
  } else {
    auto prev_buffer = std::move(log_buffer_);
    for (const auto &record : prev_buffer) {
      // 将日志中的内容进行编码
      auto encoded_record = record.encode();

      // 写入到日志文件中（写入磁盘）
      log_file_.append(encoded_record);
    }

    // 检验是否立即写入磁盘中
    if (!log_file_.sync()) {
      throw std::runtime_error("WAL file failed to sync");
    }

    // 写入后如果超过当前文件的阈值，需要重新设置一个文件
    auto file_size = log_file_.size();
    if (file_size > file_size_limit_) {
      reset_file();
    }
  }
}
// 清理WAL文件
void WAL::cleanWALFile() {
  // 遍历日志文件目录
  std::string cur_path;
  std::unique_lock<std::mutex> lock(mutex_);

  /*
    active_log_path_的格式为 /xx/xx/xx/wal.seq
    当前的wal路径中/是存在的，说明保存在当前目录的子目录下，需要提取/xx/xx/xx/作为wal_file_path
    不存在/时，说明日志文件就是保存在当前目录下，直接获取即可
  */
  if (active_log_path_.find("/") != std::string::npos) {
    cur_path =
        active_log_path_.substr(0, active_log_path_.find_last_of("/")) + "/";
  } else {
    cur_path = "./";
  }
  lock.unlock();

  // wal保存{seq, 文件的完整路径} 比如 {1, /xx/xx/xx/wal.1}
  std::vector<std::pair<size_t, std::string>> wal_paths;

  for (const auto &entry : std::filesystem::directory_iterator(cur_path)) {
    if (entry.is_regular_file() &&
        entry.path().filename().string().substr(0, 4) == "wal.") {
      std::string filename = entry.path().filename().string();
      std::string seq_str = filename.substr(4);
      uint64_t seq = std::stoull(seq_str);
      wal_paths.push_back({seq, entry.path().string()});
    }
  }

  // seq升序排序
  std::sort(wal_paths.begin(), wal_paths.end(),
            [](const std::pair<size_t, std::string> &a,
               const std::pair<size_t, std::string> &b) {
              return a.first < b.first;
            });

  // 判断是否删除
  std::vector<FileObj> del_paths;

  // 遍历除了最后一个活跃的日志外的其他日志文件
  for (int idx = 0; idx < wal_paths.size() - 1; idx++) {
    auto path = wal_paths[idx].second;
    auto file = FileObj::open(path, false);

    // 读取所有的事务id，判断与检查点的id的大小关系
    size_t offset = 0;
    bool can_delete = true; // 结束标志

    // 确保在读取文件时不会越界,如果到达末尾，下一层向后2字节读取长度的时候就会非法
    while (offset + sizeof(uint16_t) < file.size()) {
      uint16_t re_size = file.read_uint16(offset);
      uint64_t re_tranc_id = file.read_uint64(offset + sizeof(uint16_t));

      // 当前的事务id大于检查点的id，说明该事务是较新的事务，不能删除
      // 之前已经升序操作，所以这个操作较新，说明后续的事务也是新的事务，不能删除，需要跳出循环
      if (re_tranc_id > checkpoint_tranc_id_) {
        can_delete = false;
        break;
      }
    }

    // 需要删除时，将删除的内容添加
    if (can_delete) {
      del_paths.push_back(std::move(file));
    }
  }

  // 依次进行删除操作
  for (auto &del_file : del_paths) {
    del_file.del_file();
  }
}

void WAL::cleaner() {
  // TODO Lab 5.4 : 实现WAL的清理线程

  // 每次隔clean_interval_秒后进行清理，直到stop_cleaner_
  while(1){
    {
      // 睡眠 clean_interval_ s
      std::this_thread::sleep_for(std::chrono::seconds(clean_interval_));
      if (stop_cleaner_) {
        break;
      }
      cleanWALFile();
    }
  }
}

// 当前wal文件容量超出阈值后, 创建新的文件, 将seq自增
void WAL::reset_file() {
  auto old_path = active_log_path_;
  auto seq = std::stoi(old_path.substr(4));

  seq++;
  active_log_path_ = old_path.substr(0, 4) + std::to_string(seq);

  // 创建新的文件
  // log_file_.~FileObj();
  log_file_ = FileObj::create_and_write(active_log_path_, {});
}


} // namespace tiny_lsm