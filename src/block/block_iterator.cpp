#include "../../include/block/block_iterator.h"
#include "../../include/block/block.h"
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>

class Block;

namespace toni_lsm {
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

  
  auto key_index = block->get_idx_binary(key, tranc_id);

  // 查看当前的block的key是否在已有的多个block中存在
  if(key_index.has_value()){ //存在时定位到该位置
    current_index = key_index.value();
  } else { //不存在时定位到末尾
    current_index = block->offsets.size();
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

  return &(*cached_value);
}

BlockIterator &BlockIterator::operator++() {
  if (block && current_index < block->size()) {
    auto prev_idx = current_index;
    auto prev_offset = block->get_offset_at(prev_idx);
    auto prev_entry = block->get_entry_at(prev_offset);

    ++current_index;

    // 跳过相同的key
    while (block && current_index < block->size()) {
      auto cur_offset = block->get_offset_at(current_index);
      auto cur_entry = block->get_entry_at(cur_offset);
      if (cur_entry.key != prev_entry.key) {
        break;
      }
      ++current_index;
    }

    skip_by_tranc_id();
  }
  return *this;
}

bool BlockIterator::operator==(const BlockIterator &other) const {
  // TODO: Lab3.2 == 重载
  if(block == nullptr && other.block == nullptr){
    return true;
  }

  if(block == other.block && current_index == other.current_index){
    return true;
  }

  return false;
}

bool BlockIterator::operator!=(const BlockIterator &other) const {
  // TODO: Lab3.2 != 重载

  return !(*this==other);
}

BlockIterator::value_type BlockIterator::operator*() const {
  // TODO: Lab3.2 * 重载
  //block不存在或者当前block的索引超出了block的大小
  if(!block || current_index >= block->size()){
    throw std::out_of_range("Iterator out of range");
  }

  if(!cached_value.has_value()){
    size_t offset = block->get_offset_at(current_index);
    cached_value = std::make_pair(block->get_key_at(offset),block->get_value_at(offset));
  }
  return *cached_value;
}

bool BlockIterator::is_end() { return current_index == block->offsets.size(); }

void BlockIterator::update_current() const {
  // TODO: Lab3.2 更新当前指针
  // ? 该函数是可选的实现, 你可以采用自己的其他方案实现->, 而不是使用
  // ? cached_value 来缓存当前指针
  if(!cached_value.has_value() && current_index < block->offsets.size()){
    auto offset = block->get_offset_at(current_index);
    cached_value = std::make_pair(block->get_key_at(offset),block->get_value_at(offset));
  }

}

void BlockIterator::skip_by_tranc_id() {
  if (tranc_id_ == 0) {
    cached_value = std::nullopt;
    return;
  }

  while (current_index < block->offsets.size()) {
    size_t offset = block->get_offset_at(current_index);
    auto tranc_id = block->get_tranc_id_at(offset);
    if (tranc_id <= tranc_id_) {
      break;
    }
    ++current_index;
  }
  cached_value = std::nullopt;
}

} // namespace toni_lsm