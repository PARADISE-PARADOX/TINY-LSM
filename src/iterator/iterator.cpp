#include "../../include/iterator/iterator.h"
#include <tuple>
#include <utility>
#include <vector>

namespace toni_lsm {

// *************************** SearchItem ***************************
bool operator<(const SearchItem &a, const SearchItem &b) {
  // TODO: Lab2.2 实现比较规则

  //先根据键的值判断大小
  if(a.key_!=b.key_){
    return a.key_<b.key_;
  }
  //键相同,根据事务id判断大小
  if(a.tranc_id_!=b.tranc_id_){
    return a.tranc_id_>b.tranc_id_;
  }

  //再根据不同的sst层级判断大小
  if(a.level_!=b.level_){
    return a.level_<b.level_;
  }
  
  //最后判断的是跳表的层级
  return a.idx_<b.idx_;
}

bool operator>(const SearchItem &a, const SearchItem &b) {
  // TODO: Lab2.2 实现比较规则
   //先根据键的值判断大小
  if(a.key_!=b.key_){
    return a.key_>b.key_;
  }
  //键相同,根据事务id判断大小
  if(a.tranc_id_!=b.tranc_id_){
    return a.tranc_id_<b.tranc_id_;
  }

  //再根据不同的sst层级判断大小
  if(a.level_!=b.level_){
    return a.level_>b.level_;
  }
  
  //最后判断的是跳表的层级
  return a.idx_>b.idx_;
}

bool operator==(const SearchItem &a, const SearchItem &b) {
  // TODO: Lab2.2 实现比较规则
  if(a.key_!=b.key_){
    return false;
  }
  if(a.tranc_id_!=b.tranc_id_){
    return false;
  }
  if(a.level_!=b.level_){
    return false;
  }
  if(a.idx_!=b.idx_){
    return false;
  }
  return true;
}

// *************************** HeapIterator ***************************
HeapIterator::HeapIterator(std::vector<SearchItem> item_vec,
                           uint64_t max_tranc_id)
    : max_tranc_id_(max_tranc_id) {
  // TODO: Lab2.2 实现 HeapIterator 构造函数

  // 放入最小堆中
  for(auto& item:item_vec){
    items.push(item);
  }

  while(!top_value_legal()){
    // 跳过当前不可见事务
    skip_by_tranc_id();

    //将值为空的元素（标记为删除的元素）从最小堆中删除
    while(!items.empty() && items.top().value_.empty()){
      auto del_element = items.top();

      while(!items.empty() && items.top().key_== del_element.key_){
        items.pop();
      }
    }
  }
  update_current();

}

HeapIterator::pointer HeapIterator::operator->() const {
  // TODO: Lab2.2 实现 -> 重载
  // ->用于获取节点的值
  update_current();

  return current.get();
}

HeapIterator::value_type HeapIterator::operator*() const {
  // TODO: Lab2.2 实现 * 重载
  if(items.empty()) {
    return {}; // 返回空键值对
  }
  auto& top = items.top();
  return std::make_pair(top.key_, top.value_);

}

BaseIterator &HeapIterator::operator++() {
  // TODO: Lab2.2 实现 ++ 重载
  if(items.empty()){
    return *this;
  }

  //每次迭代器移动后，需要重新判断堆顶元素是否合法，以及判断堆顶元素是否是需要删除的元素，如果是就需要删除
  auto old_elemet = items.top();
  items.pop();

  //当前的堆顶元素与原本的堆顶元素的key相等，批量删除
  while(!items.empty() && old_elemet.key_ == items.top().key_){
    items.pop();
  }

  // 构造函数相同的逻辑，将需要删除的元素
  while(!top_value_legal()){
    // 跳过当前不可见事务
    skip_by_tranc_id();

    //将值为空的元素（标记为删除的元素）从最小堆中删除
    while(!items.empty() && items.top().value_.empty()){
      auto del_element = items.top();

      while(!items.empty() && items.top().key_==del_element.key_){
        items.pop();
      }
    }
  }

  return *this;
}

bool HeapIterator::operator==(const BaseIterator &other) const {
  // TODO: Lab2.2 实现 == 重载
  // ! 自行书写
 if (other.get_type() != IteratorType::HeapIterator) {
    return false;
  }

  auto HeapOther = dynamic_cast<const HeapIterator &>(other);

  if (items.empty() && HeapOther.items.empty()) {
    return true;
  }
  if (items.empty() || HeapOther.items.empty()) {
    return false;
  }

  return current == HeapOther.current;
}

bool HeapIterator::operator!=(const BaseIterator &other) const {
  // TODO: Lab2.2 实现 != 重载
  return !(*this == other);
}

bool HeapIterator::top_value_legal() const {
  // TODO: Lab2.2 判断顶部元素是否合法
  // ? 被删除的值是不合法
  // ? 不允许访问的事务创建或更改的键值对不合法(暂时忽略)
  if (items.empty()) {
    return true;
  }

  if(items.top().value_ == ""){
    return false;
  }

  if(max_tranc_id_==0){
    return items.top().value_.size() > 0;
  }

  if (items.top().tranc_id_ <= max_tranc_id_) {
    // 事务id可见, 则判断其value是否为空
    return items.top().value_.size() > 0;
  } else {
    // 事务id不可见, 即不合法
    return false;
  }
}

void HeapIterator::skip_by_tranc_id() {
  // TODO: Lab2.2 后续的Lab实现, 只是作为标记提醒
  if (max_tranc_id_ == 0) {
    // 没有开启事务
    return;
  }


  //事务id大最大id的是不可见的，所以从堆中删除
  while(!items.empty() && items.top().tranc_id_>max_tranc_id_){
    items.pop();
  }

}

bool HeapIterator::is_end() const { return items.empty(); }
bool HeapIterator::is_valid() const { return !items.empty(); }

void HeapIterator::update_current() const {
  // current 缓存了当前键值对的值, 你实现 -> 重载时可能需要
  // TODO: Lab2.2 更新当前缓存值
  if(items.empty()){
    return;
  }
  current = std::make_shared<value_type>(items.top().key_, items.top().value_);
}

IteratorType HeapIterator::get_type() const {
  return IteratorType::HeapIterator;
}

uint64_t HeapIterator::get_tranc_id() const { return max_tranc_id_; }
} // namespace toni_lsm