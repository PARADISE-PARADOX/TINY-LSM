#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <utility>

namespace toni_lsm {

//枚举迭代器的类型
enum class IteratorType {
  SkipListIterator,
  MemTableIterator,
  SstIterator,
  HeapIterator,
  TwoMergeIterator,
  ConcactIterator,
  LevelIterator,
  Undefined,
};

class BaseIterator {
public:
  using value_type = std::pair<std::string, std::string>; //键值对的类型别名为value_type
  using pointer = value_type *; //指向键值对的指针
  using reference = value_type &; //对象的引用

  virtual BaseIterator &operator++() = 0; //前置自增运算符，让迭代器移动到下一个元素

  /*
    下述的虚函数有如下特征：
    - const关键字，在成员函数声明的末尾添加const，表明不修改调用对象。
    - =0 声明为纯虚函数，无法被实例化，只能作为基本类被其他类继承，派生类需要实现所有的纯虚函数，否则也会被视为抽象类
    （包含纯虚函数的类为抽象类）
  */
  virtual bool operator==(const BaseIterator &other) const = 0; //迭代器是否相等
  virtual bool operator!=(const BaseIterator &other) const = 0; //是否不相等
  virtual value_type operator*() const = 0; //解引用重载
  virtual IteratorType get_type() const = 0; //获取迭代器类型
  virtual uint64_t get_tranc_id() const = 0; // 获取事务ID
  virtual bool is_end() const = 0; //是否到达末尾
  virtual bool is_valid() const = 0; //判断迭代器是否有效
};

class SstIterator; //类的前向声明
// *************************** SearchItem ***************************
struct SearchItem {
  std::string key_;
  std::string value_;
  uint64_t tranc_id_;
  int idx_;
  int level_; // 来自sst的level

  SearchItem() = default; //默认的构造函数，使用 = default 让编译器自动生成默认的构造函数实现，
  SearchItem(std::string k, std::string v, int i, int l, uint64_t tranc_id)
      : key_(std::move(k)), value_(std::move(v)), idx_(i), level_(l),
        tranc_id_(tranc_id) {} //初始化
};

bool operator<(const SearchItem &a, const SearchItem &b); 
bool operator>(const SearchItem &a, const SearchItem &b);
bool operator==(const SearchItem &a, const SearchItem &b);

// *************************** HeapIterator ***************************
class HeapIterator : public BaseIterator {
  friend class SstIterator; //友元，SstIterator可以访问HeapIterator的私有成员和保护成员

public:
  HeapIterator() = default;
  HeapIterator(std::vector<SearchItem> item_vec, uint64_t max_tranc_id);
  pointer operator->() const; //重载箭头运算符，返回指向当前元素的指针。
  virtual value_type operator*() const override; //重载解引用运算符，返回当前元素
  BaseIterator &operator++() override; //++运算符，让迭代器移动到下一个元素。
  BaseIterator operator++(int) = delete; //禁用后置自增运算符，= delete 表示该函数被删除，不能被调用。
  virtual bool operator==(const BaseIterator &other) const override;
  virtual bool operator!=(const BaseIterator &other) const override;

  virtual IteratorType get_type() const override;
  virtual uint64_t get_tranc_id() const override;
  virtual bool is_end() const override;
  virtual bool is_valid() const override;

private:
  bool top_value_legal() const; //判断优先队列顶部元素是否合法。
  void skip_by_tranc_id();  // 跳过当前不可见事务的id (如果开启了事务功能)
  void update_current() const; //更新当前元素。

private:
  std::priority_queue<SearchItem, std::vector<SearchItem>,
                      std::greater<SearchItem>> //std::greater<SearchItem>表示最小堆
      items;
  mutable std::shared_ptr<value_type> current; // 用于存储当前元素，mutable允许在const成员函数中修改成员变量
  uint64_t max_tranc_id_ = 0;
};
} // namespace toni_lsm