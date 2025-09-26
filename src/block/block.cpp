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
#include <sys/types.h>

namespace tiny_lsm {
Block::Block(size_t capacity) : capacity(capacity) {}

std::vector<uint8_t> Block::encode(bool with_hash) {
  // TODO Lab 3.1 编码单个类实例形成一段字节数组

  // 计算整个block的大小 数据段大小+偏移段大小+额外段大小
  size_t block_bytes = data.size() * sizeof(uint8_t) +
                       offsets.size() * sizeof(uint16_t) + sizeof(uint16_t);

  // hash如果需要，需要添加四个字节空间存储hash值
  if (with_hash) {
    block_bytes += sizeof(uint32_t);
  }

  std::vector<uint8_t> encoded(block_bytes, 0);

  //数据段放入结果中，dest表示其实位置，src表示要复制内容的起始位置，n表示复制的字节数
  size_t pos = 0;
  memcpy(encoded.data(), data.data(), data.size() * sizeof(uint8_t));
  pos += data.size() * sizeof(uint8_t);

  //偏移段
  memcpy(encoded.data() + pos, offsets.data(),
         offsets.size() * sizeof(uint16_t));
  pos += offsets.size() * sizeof(uint16_t);

  //额外段
  uint16_t num_elements = offsets.size();
  memcpy(encoded.data() + pos, &num_elements, sizeof(uint16_t));
  pos += sizeof(uint16_t);

  // hash计算，创建一个视图的模板，保持元数据的不变，hash的第一个参数是要哈希的内容指针，第二个是要哈希的内容长度
  if (with_hash) {
    uint32_t hash_val = std::hash<std::string_view>{}(
        std::string_view(reinterpret_cast<const char *>(encoded.data()),
                         encoded.size() * sizeof(uint8_t) - sizeof(uint32_t)));

    memcpy(encoded.data() + pos, &hash_val, sizeof(uint32_t));
  }

  return encoded;
}

std::shared_ptr<Block> Block::decode(const std::vector<uint8_t> &encoded,
                                     bool with_hash) {
  // TODO Lab 3.1 解码字节数组形成类实例

  // make_shared创建对象
  auto block = std::make_shared<Block>();

  //合法性检查
  if (with_hash && encoded.size() <= sizeof(uint16_t) + sizeof(uint32_t)) {
    throw std::runtime_error("Encoded data too small");
  }

  //从后往前读取encoded中的内容
  uint16_t num_elements;

  //先减去元素数量的两个字节
  size_t num_elements_pos = encoded.size() - sizeof(uint16_t);

  if (with_hash) {
    //如果hash存在，还要减去hash的四字节才能到num_elments的位置
    num_elements_pos -= sizeof(uint32_t);
    size_t hash_pos = encoded.size() - sizeof(uint32_t);

    uint32_t hash_val;
    memcpy(&hash_val, encoded.data() + hash_pos, sizeof(uint32_t));

    uint32_t computed_hash = std::hash<std::string_view>{}(
        std::string_view(reinterpret_cast<const char *>(encoded.data()),
                         encoded.size() - sizeof(uint32_t)));

    if (hash_val != computed_hash) {
      throw std::runtime_error("Block hash verification failed");
    }
  }
  memcpy(&num_elements, encoded.data() + num_elements_pos, sizeof(uint16_t));

  // 验证数据大小，required_size：偏移段的大小，每个偏移量是uint16_t类型(2字节)
  // 再加上元素数量的大小
  size_t required_size = sizeof(uint16_t) + num_elements * sizeof(uint16_t);
  if (encoded.size() < required_size) {
    throw std::runtime_error("Invalid encoded data size");
  }

  // 计算各段位置
  size_t offsets_start = num_elements_pos - num_elements * sizeof(uint16_t);

  // 读取偏移数组
  block->offsets.resize(num_elements);
  memcpy(block->offsets.data(), encoded.data() + offsets_start,
         num_elements * sizeof(uint16_t));

  block->data.reserve(offsets_start); //优化内存分配
  block->data.assign(encoded.begin(), encoded.end() + offsets_start);

  return block;
}

std::string Block::get_first_key() {
  if (data.empty() || offsets.empty()) {
    return "";
  }

  // 读取第一个key的长度（前2字节）
  uint16_t key_len;
  memcpy(&key_len, data.data(), sizeof(uint16_t));

  // 读取key
  std::string key(reinterpret_cast<char *>(data.data() + sizeof(uint16_t)),
                  key_len);
  return key;
}

size_t Block::get_offset_at(size_t idx) const {
  if (idx > offsets.size()) {
    throw std::runtime_error("idx out of offsets range");
  }
  return offsets[idx];
}

bool Block::add_entry(const std::string &key, const std::string &value,
                      uint64_t tranc_id, bool force_write) {
  // TODO Lab 3.1 添加一个键值对到block中
  // ? 返回值说明：
  // ? true: 成功添加
  // ? false: block已满, 拒绝此次添加

  // 在头文件中存在一个capacity，表示当前的最大容量，添加后的大小要小于这个值
  // 这里的三个uint16_t中，分别是key_len大小，value_len的大小，offset的偏移量大小
  if (!force_write &&
      cur_size() + key.size() + value.size() + 3 * sizeof(uint16_t) +
              sizeof(uint64_t) >
          capacity &&
      !offsets.empty()) {
    return false;
  }

  // 计算entry大小
  size_t enrty_size = sizeof(uint16_t) + key.size() + sizeof(uint16_t) +
                      value.size() + sizeof(uint64_t);

  size_t old_size = data.size();
  data.resize(old_size + enrty_size);
  size_t pos = old_size;

  // 写入key长度
  uint16_t key_len = key.size();
  memcpy(data.data() + pos, &key_len, sizeof(uint16_t));
  pos += sizeof(uint16_t);

  // 写入key
  memcpy(data.data() + pos, key.data(), key_len);
  pos += key_len;

  // value长度
  uint16_t val_len = value.size();
  memcpy(data.data() + pos, &val_len, sizeof(uint16_t));
  pos += sizeof(uint16_t);

  // value
  memcpy(data.data() + pos, value.data(), val_len);
  pos += val_len;

  // tranc_id
  memcpy(data.data() + pos, &tranc_id, sizeof(uint64_t));

  // 写入偏移offset
  offsets.push_back(old_size);

  return true;
}

// 从指定偏移量获取entry的key
std::string Block::get_key_at(size_t offset) const {
  // TODO Lab 3.1 从指定偏移量获取entry的key

  uint16_t key_len;

  //使用偏移量直接获取到key的长度
  memcpy(&key_len, data.data() + offset, sizeof(uint16_t));

  std::string cur_key = std::string(
      reinterpret_cast<const char *>(data.data() + offset + sizeof(uint16_t)),
      key_len);

  return cur_key;
}

// 从指定偏移量获取entry的value
std::string Block::get_value_at(size_t offset) const {
  // TODO Lab 3.1 从指定偏移量获取entry的value

  uint16_t key_len;

  //使用偏移量直接获取到key的长度
  memcpy(&key_len, data.data() + offset, sizeof(uint16_t));
  size_t pos = offset + sizeof(uint16_t) + key_len;

  //获取value的长度
  uint16_t val_len;
  memcpy(&val_len, data.data() + pos, sizeof(uint16_t));
  pos += sizeof(uint16_t);

  //获取value
  std::string cur_val =
      std::string(reinterpret_cast<const char *>(data.data() + pos), val_len);

  return cur_val;
}

uint64_t Block::get_tranc_id_at(size_t offset) const {
  // TODO Lab 3.1 从指定偏移量获取entry的tranc_id
  // ? 你不需要理解tranc_id的具体含义, 直接返回即可

  uint16_t key_len;

  //使用偏移量直接获取到key的长度
  memcpy(&key_len, data.data() + offset, sizeof(uint16_t));
  size_t pos = offset + sizeof(uint16_t) + key_len;

  //获取value的长度
  uint16_t val_len;
  memcpy(&val_len, data.data() + pos, sizeof(uint16_t));
  pos += sizeof(uint16_t) + val_len;

  //获取id
  uint64_t tranc_id;
  memcpy(&tranc_id, data.data() + pos, sizeof(uint64_t));

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

  while (right >= left) {
    int mid = left + (right - left) / 2;
    size_t mid_offset = offsets[mid];

    int flag = compare_key_at(mid_offset, key);

    if (flag == 0) {
      // 判断事务可见性
      auto valid_idx = adjust_idx_by_tranc_id(mid, tranc_id);
      if (valid_idx == -1) {
        return std::nullopt;
      }
      return valid_idx;
    } else if (flag < 0) {
      left = mid + 1;
    } else {
      right = mid - 1;
    }
  }

  return std::nullopt;
}

std::optional<
    std::pair<std::shared_ptr<BlockIterator>, std::shared_ptr<BlockIterator>>>
Block::iters_preffix(uint64_t tranc_id, const std::string &preffix) {
  // TODO Lab 3.3 获取前缀匹配的区间迭代器

  //比较大小，因为compare的返回方式与谓词逻辑相反（key比preffix小，说明要向后，但是返回的是负数，表示向前），所以带负号
  auto func = [&preffix](const std::string &key) {
    return -key.compare(0, preffix.size(), preffix);
  };

  return get_monotony_predicate_iters(tranc_id, func);
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
  // TODO: Lab 3.3 使用二分查找获取满足谓词的区间迭代器
  if (offsets.empty()) {
    return std::nullopt;
  }

  // 第一次二分查找，找到第一个满足谓词的位置
  int left = 0;
  int right = offsets.size() - 1;
  int first = -1;

  while (left <= right) {
    int mid = left + (right - left) / 2;
    size_t mid_offset = offsets[mid];
    auto mid_key = get_key_at(mid_offset);
    int direction = predicate(mid_key);
    if (direction <= 0) { // 目标在 mid 左侧
      right = mid - 1;
    } else // 目标在mid右侧
      left = mid + 1;
  }

  if (left >= offsets.size() || predicate(get_key_at(offsets[left])) != 0) {
    return std::nullopt; // 根本没有任何 key 满足谓词
  }

  first = left; // 保留下找到的第一个的位置

  // 第二次二分查找，找到最后一个满足谓词的位置
  int last = -1;
  right = offsets.size() - 1;
  while (left <= right) {
    int mid = left + (right - left) / 2;
    size_t mid_offset = offsets[mid];
    auto mid_key = get_key_at(mid_offset);
    int direction = predicate(mid_key);
    if (direction < 0) {
      right = mid - 1;
    } else
      left = mid + 1;
  }
  last = left - 1;
  // 最后进行组合
  auto it_begin =
      std::make_shared<BlockIterator>(shared_from_this(), first, tranc_id);
  auto it_end =
      std::make_shared<BlockIterator>(shared_from_this(), last + 1, tranc_id);

  return std::make_optional<std::pair<std::shared_ptr<BlockIterator>,
                                      std::shared_ptr<BlockIterator>>>(it_begin,
                                                                       it_end);
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
  // 实现 begin 迭代器

  return BlockIterator(shared_from_this(), 0, tranc_id);
}

BlockIterator Block::end() {
  // TODO Lab 3.2 获取end迭代器
  // 实现 end 迭代器
  return BlockIterator(shared_from_this(), offsets.size(), 0);
}
} // namespace tiny_lsm