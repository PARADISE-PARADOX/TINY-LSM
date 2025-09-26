#include "../../include/block/block_iterator.h"
#include "../../include/block/block.h"
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>

class Block;

namespace tiny_lsm {
BlockIterator::BlockIterator(std::shared_ptr<Block> b, size_t index,
                             uint64_t tranc_id)
    : block(b), current_index(index), tranc_id_(tranc_id),
      cached_value(std::nullopt) {
  skip_by_tranc_id();
}

BlockIterator::BlockIterator(std::shared_ptr<Block> b, const std::string &key,
                             uint64_t tranc_id)
    : block(b), tranc_id_(tranc_id), cached_value(std::nullopt) {
  // TODO: Lab3.2 创建迭代器时直接移动到指定的key位置
  // ? 你需要借助之前实现的 Block 类的成员函数

  // 获取key对应的索引值
  auto key_idx = block->get_idx_binary(key, tranc_id);
  if (key_idx) {
    // 索引值存在，定位
    current_index = key_idx.value();
  } else {
    // 索引值没有找到，指向末尾
    current_index = block->size();
  }
}

// BlockIterator::BlockIterator(std::shared_ptr<Block> b, uint64_t tranc_id)
//     : block(b), current_index(0), tranc_id_(tranc_id),
//       cached_value(std::nullopt) {
//   skip_by_tranc_id();
// }

BlockIterator::pointer BlockIterator::operator->() const {
  // TODO: Lab3.2 -> 重载

  update_current();
  // 通过缓存的值获取，std::optional<T>的解引用后获取的就是T类型
  return &(*cached_value);
}

BlockIterator &BlockIterator::operator++() {
  // TODO: Lab3.2 ++ 重载
  // ? 在后续的Lab实现事务后，你可能需要对这个函数进行返修

  if (block && current_index < block->size()) {

    //通过索引值找到偏移量，再找到entry
    auto idx = current_index;
    auto offset = block->get_offset_at(idx);
    auto entry = block->get_entry_at(offset);

    ++current_index;

    //跳过相同的key
    while (block && current_index < block->size()) {
      auto next_idx = current_index;
      auto next_offset = block->get_offset_at(next_idx);
      auto next_entry = block->get_entry_at(next_offset);

      if (next_entry.key != entry.key) {
        break;
      } else {
        ++current_index;
      }
    }

    skip_by_tranc_id();
  }
  return *this;
}

bool BlockIterator::operator==(const BlockIterator &other) const {
  // TODO: Lab3.2 == 重载

  if (block == nullptr && other.block == nullptr) {
    return true;
  }

  if (block == nullptr || other.block == nullptr) {
    return false;
  }

  bool equal = block == other.block && current_index == other.current_index;

  return equal;
}

bool BlockIterator::operator!=(const BlockIterator &other) const {
  // TODO: Lab3.2 != 重载

  return !(*this == other);
}

BlockIterator::value_type BlockIterator::operator*() const {
  // TODO: Lab3.2 * 重载
  if (!block || current_index >= block->size()) {
    throw std::out_of_range("Iterator out of range");
  }

  if (!cached_value) {
    //缓存不存在时需要从block中获取内容,并创建缓存
    auto offset = block->get_offset_at(current_index);
    cached_value =
        std::make_pair(block->get_key_at(offset), block->get_value_at(offset));
    return *cached_value;
  } else {
    return *cached_value;
  }
}

bool BlockIterator::is_end() { return current_index == block->offsets.size(); }

void BlockIterator::update_current() const {
  // TODO: Lab3.2 更新当前指针
  // ? 该函数是可选的实现, 你可以采用自己的其他方案实现->, 而不是使用
  // ? cached_value 来缓存当前指针

  // 缓存存在且不在末尾时，更新缓存
  if (!cached_value && current_index < block->size()) {
    auto offset = block->get_offset_at(current_index);
    cached_value =
        std::make_pair(block->get_key_at(offset), block->get_value_at(offset));
  }
}

void BlockIterator::skip_by_tranc_id() {
  // TODO: Lab3.2 * 跳过事务ID
  // ? 只是进行标记以供你在后续Lab实现事务功能后修改
  // ? 现在你不需要考虑这个函数

  if(tranc_id_==0) {
    //未开启事务
    cached_value = std::nullopt;
    return;
  }

  while(current_index < block->size()){
    size_t offset = block->get_offset_at(current_index);
    auto tranc_id = block->get_tranc_id_at(offset);
    if (tranc_id <= tranc_id_) {
      // 位置合法
      break;
    }
    // 否则跳过不可见事务的键值对
    ++current_index;
  }
  cached_value = std::nullopt;
}
} // namespace tiny_lsm