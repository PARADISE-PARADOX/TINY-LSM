#include "../../include/block/block.h"
#include "../../include/block/block_iterator.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace toni_lsm {
Block::Block(size_t capacity) : capacity(capacity) {}

std::vector<uint8_t> Block::encode() {
  //计算总大小 = 数据大小 + 偏移量大小
  size_t total_size = data.size() + offsets.size() * sizeof(uint16_t);

  std::vector<uint8_t> encoded(total_size,0);

  //将数据段放入encoded中
  memcpy(encoded.data(),data.data(),data.size());

  //偏移量
  size_t offset_pos = data.size();
  memcpy(encoded.data()+offset_pos,offsets.data(),offsets.size()*sizeof(uint16_t));

  return encoded;
}

std::shared_ptr<Block> Block::decode(const std::vector<uint8_t> &encoded,
                                     bool with_hash) {
  auto block = std::make_shared<Block>();

  if(encoded.size() < sizeof(uint16_t)){
    throw std::runtime_error("encoded size is less than sizeof(uint16_t)");
  }

  size_t data_end = encoded.size();
  size_t hash_size = with_hash ? sizeof(uint32_t) : 0;

  //需要检验hash
  if(with_hash){
    size_t hash_pos = encoded.size() - sizeof(uint32_t);

    //获取哈希值
    uint32_t hash_val;
    memcpy(&hash_val,encoded.data()+hash_pos,sizeof(uint32_t));

    //检验hash值
    uint32_t expect_hash = std::hash<std::string_view>{}(
    std::string_view(reinterpret_cast<const char *>(encoded.data()),
                         hash_pos)
    );

    if(hash_val!=expect_hash){
      throw std::runtime_error("hash value not match");
    }
    data_end = hash_pos;
  }

  // 从末尾开始读取偏移数组
  size_t offsets_size = 0;
  for (size_t i = data_end - sizeof(uint16_t); i >= 0; i -= sizeof(uint16_t)) {
    uint16_t offset;
    memcpy(&offset, encoded.data() + i, sizeof(uint16_t));
    if (offset > data_end) {
      break;
    }
    offsets_size++;
  }

  // 计算数据段大小
  size_t data_size = data_end - offsets_size * sizeof(uint16_t);

  // 读取数据段
  block->data.assign(encoded.begin(), encoded.begin() + data_size);

  // 读取偏移数组
  block->offsets.resize(offsets_size);
  memcpy(block->offsets.data(), encoded.data() + data_size, offsets_size * sizeof(uint16_t));

  return block;
}

std::string Block::get_first_key() {
  if (data.empty() || offsets.empty()) {
    return "";
  }

  // 使用第一个offset来获取第一个key
  return get_key_at(offsets[0]);
}

size_t Block::get_offset_at(size_t idx) const {
  if (idx > offsets.size()) {
    throw std::runtime_error("idx out of offsets range");
  }
  return offsets[idx];
}

bool Block::add_entry(const std::string &key, const std::string &value,
                      uint64_t tranc_id, bool force) {
  size_t entry_size = sizeof(uint16_t) + key.size() + sizeof(uint16_t) + value.size() + sizeof(uint64_t);
  size_t offset_size = sizeof(uint16_t);
  size_t total_size = entry_size + offset_size;

  if (!force && total_size > capacity) {
    return false;
  }

  // 添加数据
  size_t offset = data.size();
  
  // 写入key长度
  uint16_t key_len = key.size();
  data.insert(data.end(), reinterpret_cast<const uint8_t *>(&key_len),
              reinterpret_cast<const uint8_t *>(&key_len) + sizeof(uint16_t));
  // 写入key
  data.insert(data.end(), key.begin(), key.end());

  // 写入value长度
  uint16_t value_len = value.size();
  data.insert(data.end(), reinterpret_cast<const uint8_t *>(&value_len),
              reinterpret_cast<const uint8_t *>(&value_len) + sizeof(uint16_t));
  // 写入value
  data.insert(data.end(), value.begin(), value.end());

  // 写入事务id
  data.insert(data.end(), reinterpret_cast<const uint8_t *>(&tranc_id),
              reinterpret_cast<const uint8_t *>(&tranc_id) + sizeof(uint64_t));

  // 添加偏移
  offsets.push_back(offset);

  return true;
}

// 从指定偏移量获取entry的key
std::string Block::get_key_at(size_t offset) const {
  // TODO Lab 3.1 从指定偏移量获取entry的key
  uint16_t key_len;
  //读取key长度
  memcpy(&key_len,data.data()+offset,sizeof(uint16_t));

  // key的起始位置
  // auto key_start = data.data() + offset + sizeof(uint16_t);

  //  转换为char指针
  // const char* key_chars = reinterpret_cast<const char*>(key_start);

  // return std::string(key_chars, key_len);

  return std::string(reinterpret_cast<const char*>(data.data() + offset + sizeof(uint16_t)),key_len);
}

// 从指定偏移量获取entry的value
std::string Block::get_value_at(size_t offset) const {
  // TODO Lab 3.1 从指定偏移量获取entry的value
  uint16_t key_len;
  memcpy(&key_len,data.data()+offset,sizeof(uint16_t));

  //计算value长度的起始位置
  size_t value_len_start = offset + sizeof(uint16_t) + key_len;

  //获取value长度
  uint16_t value_len;
  memcpy(&value_len,data.data() + value_len_start,sizeof(uint16_t));

  // auto value_start = data.data() + value_len_start + sizeof(uint16_t);

  // const char* value_chars = reinterpret_cast<const char*>(value_start);

  // return std::string(value_chars,value_len);

  return std::string(reinterpret_cast<const char*>(data.data() + value_len_start + sizeof(uint16_t)),value_len);
}

uint16_t Block::get_tranc_id_at(size_t offset) const {
  // TODO Lab 3.1 从指定偏移量获取entry的tranc_id
  // ? 你不需要理解tranc_id的具体含义, 直接返回即可

  //读取key长度
  uint16_t key_len;
  memcpy(&key_len,data.data()+offset,sizeof(uint16_t));

  //计算value长度的起始位置
  auto value_len_start = offset + sizeof(uint16_t) + key_len;
  //获取value长度
  uint16_t value_len;
  memcpy(&value_len,data.data() + value_len_start,sizeof(uint16_t));

  //事务id的起始位置
  size_t tranc_id_start = value_len_start + sizeof(uint16_t) + value_len;
  uint64_t tranc_id;
  memcpy(&tranc_id,data.data() + tranc_id_start,sizeof(uint64_t));
  
  return tranc_id;
}

// 比较指定偏移量处的key与目标key
int Block::compare_key_at(size_t offset, const std::string &target) const {
  std::string key = get_key_at(offset);
  return key.compare(target);
}

// 相同的key连续分布, 且相同的key的事务id从大到小排布
// 这里的逻辑是找到最接近 tranc_id 的键值对的索引位置
int Block::adjust_idx_by_tranc_id(size_t idx, uint64_t tranc_id) {
  if (idx >= offsets.size()) {
    return -1; // 索引超出范围
  }

  auto target_key = get_key_at(offsets[idx]);

  if (tranc_id != 0) {
    auto cur_tranc_id = get_tranc_id_at(offsets[idx]);

    if (cur_tranc_id <= tranc_id) {
      // 当前记录可见，向前查找更接近的目标
      size_t prev_idx = idx;
      while (prev_idx > 0 && is_same_key(prev_idx - 1, target_key)) {
        prev_idx--;
        auto new_tranc_id = get_tranc_id_at(offsets[prev_idx]);
        if (new_tranc_id > tranc_id) {
          return prev_idx + 1; // 更新的记录不可见
        }
      }
      return prev_idx;
    } else {
      // 当前记录不可见，向后查找
      size_t next_idx = idx + 1;
      while (next_idx < offsets.size() && is_same_key(next_idx, target_key)) {
        auto new_tranc_id = get_tranc_id_at(offsets[next_idx]);
        if (new_tranc_id <= tranc_id) {
          return next_idx; // 找到可见记录
        }
        next_idx++;
      }
      return -1; // 没有找到满足条件的记录
    }
  } else {
    // 没有开启事务的话, 直接选择最大的事务id的记录返回
    size_t prev_idx = idx;
    while (prev_idx > 0 && is_same_key(prev_idx - 1, target_key)) {
      prev_idx--;
    }
    return prev_idx;
  }
}

bool Block::is_same_key(size_t idx, const std::string &target_key) const {
  if (idx >= offsets.size()) {
    return false; // 索引超出范围
  }
  return get_key_at(offsets[idx]) == target_key;
}

// 使用二分查找获取value
// 要求在插入数据时有序插入
std::optional<std::string> Block::get_value_binary(const std::string &key,
                                                   uint64_t tranc_id) {
  auto idx = get_idx_binary(key, tranc_id);
  if (!idx.has_value()) {
    return std::nullopt;
  }

  return get_value_at(offsets[*idx]);
}

std::optional<size_t> Block::get_idx_binary(const std::string &key,
                                            uint64_t tranc_id) {
  // TODO Lab 3.1 使用二分查找获取key对应的索引
  if (offsets.empty()) {
    return std::nullopt;
  }

  int left = 0;
  int right = offsets.size() - 1;
  while(left<=right){
    int mid = left + (right - left) / 2;
    size_t mid_offset = offsets[mid];
    int cmp = compare_key_at(mid_offset, key);
    if(cmp==0){
      int new_mid = adjust_idx_by_tranc_id(mid, tranc_id);
      if(new_mid!=-1){
        return new_mid;
      }
      return std::nullopt;
    }else if(cmp<0){
      left = mid + 1;
    }else{
      right = mid - 1;
    }
  }
  return std::nullopt;
}


std::optional<
    std::pair<std::shared_ptr<BlockIterator>, std::shared_ptr<BlockIterator>>>
Block::iters_preffix(uint64_t tranc_id, const std::string &preffix) {
  // TODO Lab 3.3 获取前缀匹配的区间迭代器

  auto prefix_lamba = [&preffix](const std::string &key){
    return -key.compare(0,preffix.size(),preffix);
  };

  return get_monotony_predicate_iters(tranc_id, prefix_lamba);

}

// 返回第一个满足谓词的位置和最后一个满足谓词的位置
// 如果不存在, 范围nullptr
// 谓词作用于key, 且保证满足谓词的结果只在一段连续的区间内, 例如前缀匹配的谓词
// 返回的区间是闭区间, 开区间需要手动对返回值自增
// predicate返回值:
//   0: 满足谓词
//   >0: 不满足谓词, 需要向右移动
//   <0: 不满足谓词, 需要向左移动


std::optional<
    std::pair<std::shared_ptr<BlockIterator>, std::shared_ptr<BlockIterator>>>
Block::get_monotony_predicate_iters(
    uint64_t tranc_id, std::function<int(const std::string &)> predicate) {
  if(offsets.empty()){
    return std::nullopt;
  }

  // 二分查找找到第一个满足谓词的位置
  int left = 0;
  int right = offsets.size() - 1;
  int first = -1;

  // 找到第一个满足谓词的位置（predicate返回0的位置）
  while(left <= right) {
    int mid = left + (right - left) / 2;
    size_t mid_offset = offsets[mid];
    auto mid_key = get_key_at(mid_offset);
    int flag = predicate(mid_key);
    
    if(flag == 0) {
      first = mid;
      right = mid - 1;  // 继续向左找第一个满足的位置
    } else if(flag > 0) {
      right = mid - 1;  // 当前key太大，向左找
    } else {
      left = mid + 1;   // 当前key太小，向右找
    }
  }

  if(first == -1) {
    // 如果没有找到恰好等于0的位置，检查left是否是第一个满足条件的位置
    if(left < offsets.size()) {
      auto key = get_key_at(offsets[left]);
      if(predicate(key) == 0) {
        first = left;
      }
    }
    if(first == -1) {
      return std::nullopt;
    }
  }

  // 寻找最后一个满足谓词的位置
  left = first;
  right = offsets.size() - 1;
  int last = first;

  // 找到最后一个满足谓词的位置（predicate返回0的位置）
  while(left <= right) {
    int mid = left + (right - left) / 2;
    size_t mid_offset = offsets[mid];
    auto mid_key = get_key_at(mid_offset);
    int flag = predicate(mid_key);
    
    if(flag == 0) {
      last = mid;
      left = mid + 1;   // 继续向右找最后一个满足的位置
    } else if(flag > 0) {
      right = mid - 1;  // 当前key太大，向左找
    } else {
      left = mid + 1;   // 当前key太小，向右找
    }
  }

  auto it_start = std::make_shared<BlockIterator>(shared_from_this(), first, tranc_id);
  auto it_end = std::make_shared<BlockIterator>(shared_from_this(), last + 1, tranc_id);

  return std::make_optional<
    std::pair<std::shared_ptr<BlockIterator>, std::shared_ptr<BlockIterator>>>(it_start, it_end);
}

Block::Entry Block::get_entry_at(size_t offset) const {
  Entry entry;
  entry.key = get_key_at(offset);
  entry.value = get_value_at(offset);
  entry.tranc_id = get_tranc_id_at(offset);
  return entry;
}

size_t Block::size() const { return offsets.size(); }

size_t Block::cur_size() const {
  return data.size() + offsets.size() * sizeof(uint16_t) + sizeof(uint16_t);
}

bool Block::is_empty() const { return offsets.empty(); }

BlockIterator Block::begin(uint64_t tranc_id) {
  // TODO Lab 3.2 获取begin迭代器
  return BlockIterator(shared_from_this(), 0, tranc_id);
}

BlockIterator Block::end() {
  // TODO Lab 3.2 获取end迭代器
  return BlockIterator(shared_from_this(), offsets.size(), 0);
}
} // namespace toni_lsm

std::string Block::get_key_at(size_t idx) const {
  if(idx >= offsets.size()){
    throw std::runtime_error("index out of range");
  }

  size_t start = offsets[idx];
  size_t end = (idx + 1 < offsets.size()) ? offsets[idx + 1] : data.size();
  size_t value_size = (end - start - sizeof(uint64_t)) / 2; // 假设key和value大小相近
  size_t key_size = end - start - sizeof(uint64_t) - value_size;

  return std::string(reinterpret_cast<const char *>(data.data() + start), key_size);
}

std::string Block::get_value_at(size_t idx) const {
  if(idx >= offsets.size()){
    throw std::runtime_error("index out of range");
  }

  size_t start = offsets[idx];
  size_t end = (idx + 1 < offsets.size()) ? offsets[idx + 1] : data.size();
  size_t value_size = (end - start - sizeof(uint64_t)) / 2; // 假设key和value大小相近
  size_t key_size = end - start - sizeof(uint64_t) - value_size;

  return std::string(reinterpret_cast<const char *>(data.data() + start + key_size), value_size);
}

size_t Block::value_size_at(size_t idx) const {
  if(idx >= offsets.size()){
    throw std::runtime_error("index out of range");
  }

  size_t start = offsets[idx];
  size_t end = (idx + 1 < offsets.size()) ? offsets[idx + 1] : data.size();
  return (end - start - sizeof(uint64_t)) / 2; // 假设key和value大小相近
}

size_t Block::value_size_at(size_t idx) const {
  if(idx >= offsets.size()){
    throw std::runtime_error("index out of range");
  }

  size_t start = offsets[idx];
  size_t end = (idx + 1 < offsets.size()) ? offsets[idx + 1] : data.size();
  size_t total_size = end - start;
  size_t key_size = get_key_at(idx).size();

  return total_size - key_size - sizeof(uint64_t);
}