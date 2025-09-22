#include "../../include/iterator/iterator.h"
#include <memory>
#include <tuple>
#include <vector>

namespace tiny_lsm {

// *************************** SearchItem ***************************
bool operator<(const SearchItem &a, const SearchItem &b) {
  // TODO: Lab2.2 实现比较规则
  // 首先比较key的大小
  if(a.key_ != b.key_){
    return a.key_ < b.key_;
  }

  //比较id的大小，设计id小的靠近堆顶
  if(a.tranc_id_ != b.tranc_id_){
    return a.tranc_id_ > b.tranc_id_;
  }

  //比较层级大小
  if(a.level_ != b.level_){
    return a.level_ < b.level_;
  }
  return a.idx_ < b.idx_;
}

bool operator>(const SearchItem &a, const SearchItem &b) {
  // TODO: Lab2.2 实现比较规则
  //! 比较的方法与设计不相符
    // 首先比较key的大小
  if(a.key_ != b.key_){
    return a.key_ > b.key_;
  }

  //比较id的大小，设计id小的靠近堆顶
  if(a.tranc_id_ != b.tranc_id_){
    return a.tranc_id_ < b.tranc_id_;
  }

  //比较层级大小
  if(a.level_ != b.level_){
    return a.level_ > b.level_;
  }
  return a.idx_ > b.idx_;
}

bool operator==(const SearchItem &a, const SearchItem &b) {
  // TODO: Lab2.2 实现比较规则
  // 这里的 == 表示两个SearchItem是源于同一个数据源（idx_）和相同的键（key_）
  return a.key_ == b.key_ && a.idx_ == b.idx_;
}

// *************************** HeapIterator ***************************

HeapIterator::HeapIterator(bool skip_delete) : skip_delete_(skip_delete) {
  // 默认构造函数
}
HeapIterator::HeapIterator(std::vector<SearchItem> item_vec,
                           uint64_t max_tranc_id, bool skip_delete)
    : max_tranc_id_(max_tranc_id), skip_delete_(skip_delete) {
  // TODO: Lab2.2 实现 HeapIterator 构造函数

  //将SearchItem放入堆中
  for (auto &item : item_vec) {
    items.push(item);
  }

  //判断顶部元素是否合法
  while(!top_value_legal()){
    //当顶部元素不合法的时候，需要去进行筛选，重新选出一个合法的顶部元素
    //跳过id不可见的部分
    skip_by_tranc_id();

    //删除的元素需要跳过
    while(!items.empty() && items.top().value_.empty()){

      //记录下需要删除的id，当堆顶的元素标记为删除的时候，后续堆中key相同的元素需要同时删除
      auto delete_key = items.top().key_;
      while(!items.empty() && items.top().key_ == delete_key){
        items.pop();
      }
    }
  }

}

HeapIterator::pointer HeapIterator::operator->() const {
  // TODO: Lab2.2 实现 -> 重载
  update_current();
  return current.get();
}

HeapIterator::value_type HeapIterator::operator*() const {
  // TODO: Lab2.2 实现 * 重载
  auto res = std::make_pair(items.top().key_,items.top().value_);
  return res;
}

BaseIterator &HeapIterator::operator++() {
  // TODO: Lab2.2 实现 ++ 重载
  //空队列直接返回
  if(items.empty()){
    return *this;
  }

  //获取原本的堆顶元素
  auto old_item = items.top();
  items.pop();

  //堆中还有可能存在和old_item相等的元素，所以需要批量删除这些元素
  while(!items.empty() && items.top().key_ == old_item.key_){
    items.pop();
  }

  //上述操作后，还需要判断顶部元素是否合法
  while(!top_value_legal()){
    //当顶部元素不合法的时候，需要去进行筛选，重新选出一个合法的顶部元素
    //跳过id不可见的部分
    skip_by_tranc_id();

    //删除的元素需要跳过
    while(!items.empty() && items.top().value_.empty()){

      //记录下需要删除的id，当堆顶的元素标记为删除的时候，后续堆中key相同的元素需要同时删除
      auto delete_key = items.top().key_;
      while(!items.empty() && items.top().key_ == delete_key){
        items.pop();
      }
    }
  }

  return *this;
}

bool HeapIterator::operator==(const BaseIterator &other) const {
  // TODO: Lab2.2 实现 == 重载
  if(other.get_type()!=IteratorType::HeapIterator){
    return false;
  }

  //动态地将类型转为HeapIterator
  auto other2 = dynamic_cast<const HeapIterator&>(other);

  if(items.empty() && other2.items.empty()){
    return true;
  }

  if(items.empty() || other2.items.empty()){
    return false;
  }

  //判断堆顶元素是否相等，因为每次只有堆顶元素表示当前位置
  bool equal = (items.top().key_ == other2.items.top().key_) && (items.top().value_ == other2.items.top().value_);

  return equal;
}

bool HeapIterator::operator!=(const BaseIterator &other) const {
  // TODO: Lab2.2 实现 != 重载
  return !(*this==other);
}

bool HeapIterator::top_value_legal() const {
  // TODO: Lab2.2 判断顶部元素是否合法
  // ? 被删除的值是不合法
  // ? 不允许访问的事务创建或更改的键值对不合法(暂时忽略)
  // 堆为空是合法
  if(items.empty()){
    return true;
  }

  //事务id为0表示未开启事务
  if(max_tranc_id_==0){
    return items.top().value_.size() > 0;
  } else if(max_tranc_id_ >= items.top().tranc_id_){
    if(items.top().value_.size() > 0){
      return true;
    } else {
      return false;
    }
  } else {
    return false;
  }
}

void HeapIterator::skip_by_tranc_id() {
  // TODO: Lab2.2 后续的Lab实现, 只是作为标记提醒
  if (max_tranc_id_ == 0) {
    // 没有开启事务
    return;
  }
  while (!items.empty() && items.top().tranc_id_ > max_tranc_id_) {
    items.pop();
  }

}

bool HeapIterator::is_end() const { return items.empty(); }
bool HeapIterator::is_valid() const { return !items.empty(); }

void HeapIterator::update_current() const {
  // current 缓存了当前键值对的值, 你实现 -> 重载时可能需要
  // TODO: Lab2.2 更新当前缓存值

  if(!items.empty()){
    current = std::make_shared<value_type>(items.top().key_, items.top().value_);
  } else {
    //释放current指针
    current.reset();
  }

}

IteratorType HeapIterator::get_type() const {
  return IteratorType::HeapIterator;
}

uint64_t HeapIterator::get_tranc_id() const { return max_tranc_id_; }
} // namespace tiny_lsm