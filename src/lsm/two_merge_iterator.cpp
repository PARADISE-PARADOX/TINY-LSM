#include "../../include/lsm/two_merge_iterator.h"

namespace tiny_lsm {

TwoMergeIterator::TwoMergeIterator() {}

TwoMergeIterator::TwoMergeIterator(std::shared_ptr<BaseIterator> it_a,
                                   std::shared_ptr<BaseIterator> it_b,
                                   uint64_t max_tranc_id)
    : it_a(std::move(it_a)), it_b(std::move(it_b)),
      max_tranc_id_(max_tranc_id) {
  // 先跳过不可见的事务
  skip_by_tranc_id();
  skip_it_b();              // 跳过与 it_a 重复的 key
  choose_a = choose_it_a(); // 决定使用哪个迭代器
}

bool TwoMergeIterator::choose_it_a() {
  // TODO: Lab 4.4: 实现选择迭代器的逻辑
  if (it_a->is_end()) {
    return false;
  }
  if (it_b->is_end()) {
    return true;
  }
  return (**it_a).first < (**it_b).first; // 比较 key
}

void TwoMergeIterator::skip_it_b() {
  if (!it_a->is_end() && !it_b->is_end() && (**it_a).first == (**it_b).first) {
    ++(*it_b);
  }
}

void TwoMergeIterator::skip_by_tranc_id() {
  // TODO: Lab xx
  if (max_tranc_id_ == 0) {
    return;
  }
  while (it_a->get_tranc_id() > max_tranc_id_) {
    ++(*it_a);
  }
  while (it_b->get_tranc_id() > max_tranc_id_) {
    ++(*it_b);
  }
}

BaseIterator &TwoMergeIterator::operator++() {
  // TODO: Lab 4.4: 实现 ++ 重载
  if (choose_a) {
    ++(*it_a);
  } else {
    ++(*it_b);
  }

  skip_by_tranc_id();       // 先跳过不可见的事务
  skip_it_b();              // 跳过两个迭代器中重复的键。
  choose_a = choose_it_a(); // 重新决定使用哪个迭代器
  return *this;
}

bool TwoMergeIterator::operator==(const BaseIterator &other) const {
  // TODO: Lab 4.4: 实现 == 重载
  if (other.get_type() != IteratorType::TwoMergeIterator) {
    return false;
  }
  auto other2 = dynamic_cast<const TwoMergeIterator &>(other);
  if (this->is_end() && other2.is_end()) {
    return true;
  }
  if (this->is_end() || other2.is_end()) {
    return false;
  }
  return it_a == other2.it_a && it_b == other2.it_b &&
         choose_a == other2.choose_a;
}

bool TwoMergeIterator::operator!=(const BaseIterator &other) const {
  // TODO: Lab 4.4: 实现 != 重载
  return !(*this == other);
}

BaseIterator::value_type TwoMergeIterator::operator*() const {
  // TODO: Lab 4.4: 实现 * 重载
  if (choose_a) {
    return **it_a;
  } else {
    return **it_b;
  }
}

IteratorType TwoMergeIterator::get_type() const {
  return IteratorType::TwoMergeIterator;
}

uint64_t TwoMergeIterator::get_tranc_id() const { return max_tranc_id_; }

bool TwoMergeIterator::is_end() const {
  if (it_a == nullptr && it_b == nullptr) {
    return true;
  }
  if (it_a == nullptr) {
    return it_b->is_end();
  }
  if (it_b == nullptr) {
    return it_a->is_end();
  }
  return it_a->is_end() && it_b->is_end();
}

bool TwoMergeIterator::is_valid() const {
  if (it_a == nullptr && it_b == nullptr) {
    return false;
  }
  if (it_a == nullptr) {
    return it_b->is_valid();
  }
  if (it_b == nullptr) {
    return it_a->is_valid();
  }
  return it_a->is_valid() || it_b->is_valid();
}

TwoMergeIterator::pointer TwoMergeIterator::operator->() const {
  // TODO: Lab 4.4: 实现 -> 重载
  update_current();
  return current.get();
}

void TwoMergeIterator::update_current() const {
  // TODO: Lab 4.4: 实现更新缓存键值对的辅助函数
  if (choose_a) {
    current = std::make_shared<value_type>(**it_a);
  } else {
    current = std::make_shared<value_type>(**it_b);
  }
}
} // namespace tiny_lsm