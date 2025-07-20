#pragma once

#include "../iterator/iterator.h"
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace toni_lsm {
class Block;

class BlockIterator {
public:
  // 标准迭代器类型定义
  using iterator_category = std::forward_iterator_tag; //前向迭代器，智能向前移动
  using value_type = std::pair<std::string, std::string>; //键值对
  using difference_type = std::ptrdiff_t; // 表示两个指针相减的结果类型（迭代器直接的距离）
  using pointer = const value_type *; // 指向值的指针
  using reference = const value_type &; // 指向值的引用

  // 构造函数
  BlockIterator(std::shared_ptr<Block> b, size_t index, uint64_t tranc_id);
  BlockIterator(std::shared_ptr<Block> b, const std::string &key,
                uint64_t tranc_id);
  // BlockIterator(std::shared_ptr<Block> b, uint64_t tranc_id);
  BlockIterator()
      : block(nullptr), current_index(0), tranc_id_(0) {} // end iterator

  // 迭代器操作
  pointer operator->() const; // 返回指向当前值的指针
  BlockIterator &operator++(); // 前向迭代器，++ 操作符，返回下一个迭代器
  BlockIterator operator++(int) = delete; // 后向迭代器，++ 操作符，因为前向迭代器，所以向后遍历的操作不合法
  bool operator==(const BlockIterator &other) const;
  bool operator!=(const BlockIterator &other) const;
  value_type operator*() const;
  bool is_end();

private:
  void update_current() const; // 更新当前值
  // 跳过当前不可见事务的id (如果开启了事务功能)
  void skip_by_tranc_id();

private:
  std::shared_ptr<Block> block;                   // 指向所属的 Block
  size_t current_index;                           // 当前位置的索引
  uint64_t tranc_id_;                             // 当前事务 id
  mutable std::optional<value_type> cached_value; // 缓存当前值，以便后续使用
};
} // namespace toni_lsm
