// src/wal/record.cpp

#include "../../include/wal/record.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sys/types.h>

namespace tiny_lsm {

Record Record::createRecord(uint64_t tranc_id) {
  // TODO: Lab 5.3 实现创建事务的Record
  Record record;

  record.tranc_id_ = tranc_id;
  record.operation_type_ = OperationType::CREATE;

  // 长度为，存储长度本身的16位 + 事务id的64位 + 事务类型的8位
  record.record_len_ = sizeof(uint16_t) + sizeof(uint64_t) + sizeof(uint8_t);

  return record;
}
Record Record::commitRecord(uint64_t tranc_id) {
  // TODO: Lab 5.3 实现提交事务的Record
  Record record;

  record.tranc_id_ = tranc_id;
  record.operation_type_ = OperationType::COMMIT;

  // 长度为，存储长度本身的16位 + 事务id的64位 + 事务类型的8位
  record.record_len_ = sizeof(uint16_t) + sizeof(uint64_t) + sizeof(uint8_t);

  return record;
}
Record Record::rollbackRecord(uint64_t tranc_id) {
  // TODO: Lab 5.3 实现回滚事务的Record
  Record record;

  record.tranc_id_ = tranc_id;
  record.operation_type_ = OperationType::ROLLBACK;

  // 长度为，存储长度本身的16位 + 事务id的64位 + 事务类型的8位
  record.record_len_ = sizeof(uint16_t) + sizeof(uint64_t) + sizeof(uint8_t);

  return record;
}
Record Record::putRecord(uint64_t tranc_id, const std::string &key,
                         const std::string &value) {
  // TODO: Lab 5.3 实现插入键值对的Record
  Record record;

  record.tranc_id_ = tranc_id;
  record.operation_type_ = OperationType::PUT;
  record.key_ = key;
  record.value_ = value;

  record.record_len_ = sizeof(uint16_t) + sizeof(uint64_t) + sizeof(uint8_t) +
                       sizeof(uint16_t) + key.size() + sizeof(uint16_t) +
                       value.size();

  return record;
}
Record Record::deleteRecord(uint64_t tranc_id, const std::string &key) {
  // TODO: Lab 5.3 实现删除键值对的Record
  Record record;

  record.tranc_id_ = tranc_id;
  record.operation_type_ = OperationType::DELETE;
  record.key_ = key;
  record.record_len_ = sizeof(uint16_t) + sizeof(uint64_t) + sizeof(uint8_t) +
                       sizeof(uint16_t) + key.size();

  return record;
}

std::vector<uint8_t> Record::encode() const {
  // TODO: Lab 5.3 实现Record的编码函数
  std::vector<uint8_t> encoded_record;

  encoded_record.resize(record_len_, 0);
  auto ptr = encoded_record.data();

  // 编码record_len
  std::memcpy(ptr, &record_len_, sizeof(uint16_t));
  ptr += sizeof(uint16_t);

  // 编码tranc_id
  std::memcpy(ptr, &tranc_id_, sizeof(uint64_t));
  ptr += sizeof(uint64_t);

  // 编码操作类型
  std::memcpy(ptr, &operation_type_, sizeof(uint8_t));
  ptr += sizeof(uint8_t);

  // 其中三个类型通过上述的编码已经完成，PUT和DELETE操作需要额外操作
  if (operation_type_ == OperationType::PUT) {
    uint16_t key_len = key_.size();

    // 添加key_len
    std::memcpy(ptr, &key_len, sizeof(uint16_t));
    ptr += sizeof(uint16_t);

    // 添加key_
    std::memcpy(ptr, &key_, key_len);
    ptr += key_len;

    // 添加value_len
    uint16_t val_len = value_.size();
    std::memcpy(ptr, &val_len, sizeof(uint16_t));
    ptr += sizeof(uint16_t);

    // 添加value
    std::memcpy(ptr, &value_, val_len);
    ptr += val_len;
  } else if (operation_type_ == OperationType::DELETE) {

    uint16_t key_len = key_.size();

    // 添加key_len
    std::memcpy(ptr, &key_len, sizeof(uint16_t));
    ptr += sizeof(uint16_t);

    // 添加key_
    std::memcpy(ptr, &key_, key_len);
    ptr += key_len;
  }

  return encoded_record;
}

std::vector<Record> Record::decode(const std::vector<uint8_t> &data) {
  // TODO: Lab 5.3 实现Record的解码函数

  // 小于最小长度，返回空
  if (data.size() < sizeof(uint16_t) + sizeof(uint64_t) + sizeof(uint8_t)) {
    return {};
  }

  std::vector<Record> records;
  size_t pos = 0;

  while(pos < data.size()){
    // 长度
    uint16_t re_len;
    std::memcpy(&re_len, data.data() + pos, sizeof(uint16_t));
    pos += sizeof(uint16_t);

    // 检查数据长度是否足够
    if (data.size() < re_len) {
      throw std::runtime_error("Data length does not match record length");
    }

    // 读取tranc_id
    uint64_t re_tranc_id;
    std::memcpy(&re_tranc_id, data.data() + pos, sizeof(uint64_t));
    pos += sizeof(uint64_t);

    // 读取操作类型
    uint8_t op;
    std::memcpy(&op, data.data() + pos, sizeof(uint8_t));
    OperationType re_op = static_cast<OperationType>(op);
    pos += sizeof(uint8_t);

    Record record;
    record.record_len_ = re_len;
    record.tranc_id_ = re_tranc_id;
    record.operation_type_ = re_op;

    if (record.operation_type_ == OperationType::PUT) {
      // 读取key_len
      uint16_t k_l;
      std::memcpy(&k_l, data.data() + pos, sizeof(uint16_t));
      pos += sizeof(uint16_t);

      // 读取key
      std::string re_k;
      std::memcpy(&re_k, data.data() + pos, k_l);
      pos += k_l;

      // 读取value_len
      uint16_t val_l;
      std::memcpy(&val_l, data.data() + pos, sizeof(uint16_t));
      pos += sizeof(uint16_t);

      std::string re_val;
      std::memcpy(&re_val, data.data() + pos, k_l);
      pos += val_l;
    } else if(record.operation_type_ == OperationType::DELETE){
      // 读取key_len
      uint16_t k_l;
      std::memcpy(&k_l, data.data() + pos, sizeof(uint16_t));
      pos += sizeof(uint16_t);

      // 读取key
      std::string re_k;
      std::memcpy(&re_k, data.data() + pos, k_l);
      pos += k_l;
    }
    records.push_back(record);
  }
  return records;
}
void Record::print() const {
  std::cout << "Record: tranc_id=" << tranc_id_
            << ", operation_type=" << static_cast<int>(operation_type_)
            << ", key=" << key_ << ", value=" << value_ << std::endl;
}

bool Record::operator==(const Record &other) const {
  if (tranc_id_ != other.tranc_id_ ||
      operation_type_ != other.operation_type_) {
    return false;
  }

  // 不需要 key 和 value 比较的情况
  if (operation_type_ == OperationType::CREATE ||
      operation_type_ == OperationType::COMMIT ||
      operation_type_ == OperationType::ROLLBACK) {
    return true;
  }

  // 需要 key 比较的情况
  if (operation_type_ == OperationType::DELETE) {
    return key_ == other.key_;
  }

  // 需要 key 和 value 比较的情况
  return key_ == other.key_ && value_ == other.value_;
}

bool Record::operator!=(const Record &other) const { return !(*this == other); }
} // namespace tiny_lsm