#include "../../include/skiplist/skiplist.h"
#include <cstdint>
#include <iostream>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace tiny_lsm {

// ************************ SkipListIterator ************************
BaseIterator &SkipListIterator::operator++() {
  // TODO: Lab1.2 任务：实现SkipListIterator的++操作符
  if(current){
    current = current->forward_[0];
  }
  return *this;
}

bool SkipListIterator::operator==(const BaseIterator &other) const {
  // TODO: Lab1.2 任务：实现SkipListIterator的==操作符
  if(other.get_type() != IteratorType::SkipListIterator){
    return false;
  }
  auto other2 = dynamic_cast<const SkipListIterator&>(other);
  if(other2.current != current){
    return false;
  }
  return true;
}

bool SkipListIterator::operator!=(const BaseIterator &other) const {
  // TODO: Lab1.2 任务：实现SkipListIterator的!=操作符
  return !(*this == other);
}

SkipListIterator::value_type SkipListIterator::operator*() const {
  // TODO: Lab1.2 任务：实现SkipListIterator的*操作符

  if(current){
    value_type res = std::make_pair(current->key_,current->value_);
    return res;
  } else {
    throw std::runtime_error("current is invalid , is invalid iterator");
  }
}

IteratorType SkipListIterator::get_type() const {
  // TODO: Lab1.2 任务：实现SkipListIterator的get_type
  // ? 主要是为了熟悉基类的定义和继承关系

  auto iter_type = IteratorType::SkipListIterator;
  return iter_type;
}

bool SkipListIterator::is_valid() const {
  return current && !current->key_.empty();
}
bool SkipListIterator::is_end() const { return current == nullptr; }

std::string SkipListIterator::get_key() const { return current->key_; }
std::string SkipListIterator::get_value() const { return current->value_; }
uint64_t SkipListIterator::get_tranc_id() const { return current->tranc_id_; }

// ************************ SkipList ************************
// 构造函数
SkipList::SkipList(int max_lvl) : max_level(max_lvl), current_level(1) {
  head = std::make_shared<SkipListNode>("", "", max_level, 0);
  dis_01 = std::uniform_int_distribution<>(0, 1);
  dis_level = std::uniform_int_distribution<>(0, (1 << max_lvl) - 1);
  gen = std::mt19937(std::random_device()());
}

int SkipList::random_level() {
  // ? 通过"抛硬币"的方式随机生成层数：
  // ? - 每次有50%的概率增加一层
  // ? - 确保层数分布为：第1层100%，第2层50%，第3层25%，以此类推
  // ? - 层数范围限制在[1, max_level]之间，避免浪费内存
  // TODO: Lab1.1 任务：插入时随机为这一次操作确定其最高连接的链表层数
  return 0;
}

// 插入或更新键值对
void SkipList::put(const std::string &key, const std::string &value,
                   uint64_t tranc_id) {
  spdlog::trace("SkipList--put({}, {}, {})", key, value, tranc_id);
  // ? Hint: 你需要保证不同`Level`的步长从底层到高层逐渐增加
  // ? 你可能需要使用到`random_level`函数以确定层数, 其注释中为你提供一种思路
  // ? tranc_id 为事务id, 现在你不需要关注它, 直接将其传递到 SkipListNode 的构造函数中即可


  //记录每一层需要插入节点的前驱节点的位置，数组的索引为层级数，update[1]表示第一层级的前驱节点，要插入的节点就在前驱节点的后面
  std::vector<std::shared_ptr<SkipListNode>> update(max_level, nullptr);
    
  //随机确定新节点的层数，该节点的层级不能超过当前的层级
  int new_level = std::max(random_level(), current_level);

  //创建一个新节点
  auto new_node = std::make_shared<SkipListNode>(key,value,new_level,tranc_id);

  //从最高层查找插入的顺序
  auto current = head; //head是在头文件中定义的跳表的头结点

  for(int level=current_level-1;level>=0;level--){
    //当前层中寻找小于key的最后一个节点
    while(current->forward_[level] && current->forward_[level]->key_ < key){
      current = current->forward_[level];
    }
    update[level] = current;

    spdlog::trace("SkipList--put({}, {}, {}), level{} needs updating", key,
                  value, tranc_id, level);
  }
 
  // 移动到最底层，当节点在非底层被找到的时候，还是需要在最底层进行确认
  current = current->forward_[0];

  // 检查是否已存在相同key和tranc_id的节点
  if(current && current->key_ == key && current->tranc_id_ == tranc_id){
    //更新内存大小,数值和事务id
    size_bytes += value.size() - current->value_.size();
    current->value_ = value;
    current->tranc_id_ = tranc_id;

    spdlog::trace("SkipList--put({}, {}, {}), key and tranc_id_ is the same, "
                  "only update value to {}",
                  key, value, tranc_id, value);

    return;
  }
  
  //当key的值不存在的时候，创建新的节点
   // ! 默认新的 tranc_id 一定比当前的大, 由上层保证
  if(new_level>current_level){

    //从当前的层级到新层级中，将head设置为这些层的前驱节点。
    for(int i=current_level;i<new_level;i++){
      update[i] = head;
      spdlog::trace("SkipList--put({}, {}, {}), update level{} to head", key,
                    value, tranc_id, i);
    }
    current_level = new_level; 
  }

  // 生成一个随机数，用于决定是否在每一层更新节点
  int random_bits = dis_level(gen);

  size_bytes += key.size() + value.size() + sizeof(uint64_t);

  //更新各层的指针
  for (int i = 0; i < new_level; ++i) {
    bool need_update = false;
    if(i==0 || new_level>current_level || (random_bits & (1 << i)) ){
      /*
        按照如下顺序判断是否进行更新
        - 第0层总是更新
        - 如果需要创建新的层级, 这个节点需要再之前所有的层级上都更新
        - 否则, 根据随机数的位数按照50%的概率更新
      */
      need_update = true;
    }

    // 插入节点的方式：
    /*
      比如在AC之间插入B      A <-> C
        1. 让B的forward指向C
        2. C的backward指向B
        3. 让A的forward指向B
        4. B的backward指向A
    */
    if (need_update) {
      new_node->forward_[i] = update[i]->forward_[i]; // 可能为nullptr
      if (new_node->forward_[i]) {
        new_node->forward_[i]->set_backward(i, new_node);
      }
      update[i]->forward_[i] = new_node;
      new_node->set_backward(i, update[i]);
    } else {
      // 如果不更新当前层，之后更高的层级都不更新
      break;
    }
  }
  current_level = new_level;

}


// 查找键值对
SkipListIterator SkipList::get(const std::string &key, uint64_t tranc_id) {
  // std::shared_lock<std::shared_mutex> slock(rw_mutex);
  spdlog::trace("SkipList--get({}) called", key);

  auto current = head;
  // 从最高层开始查找
  for (int i = current_level - 1; i >= 0; --i) {
    while (current->forward_[i] && current->forward_[i]->key_ < key) {
      current = current->forward_[i];
    }
  }
  // 移动到最底层
  current = current->forward_[0];
  if (tranc_id == 0) {
    // 如果没有开启事务，直接比较key即可
    // 如果找到匹配的key，返回value
    if (current && current->key_ == key) {
      return SkipListIterator{current};
    }
  } else {
    while (current && current->key_ == key) {
      // 如果开启了事务，只返回小于等于事务id的值
      if (tranc_id != 0) {
        if (current->tranc_id_ <= tranc_id) {
          // 满足事务可见性
          return SkipListIterator{current};

        } else {
          // 否则跳过
          current = current->forward_[0];
        }
      } else {
        // 没有开启事务
        return SkipListIterator{current};
      }
    }
  }
  // 未找到返回空
  spdlog::trace("SkipList--get({}): not found", key);
  return SkipListIterator{};
}

// 删除键值对
// ! 这里的 remove 是跳表本身真实的 remove,  lsm 应该使用 put 空值表示删除,
// ! 这里只是为了实现完整的 SkipList 不会真正被上层调用
void SkipList::remove(const std::string &key) {
  std::vector<std::shared_ptr<SkipListNode>> update(max_level, nullptr);

  // std::unique_lock<std::shared_mutex> lock(rw_mutex);
  auto current = head;

  // 从最高层开始查找目标节点
  for (int i = current->forward_.size() - 1; i >= 0; --i) {
    while (current->forward_[i] && current->forward_[i]->key_ < key) {
      current = current->forward_[i];
    }
    update[i] = current;
  }

  // 移动到最底层
  current = current->forward_[0];

  // 如果找到目标节点，执行删除操作
  if (current && current->key_ == key) {
    // 更新每一层的 forward 指针，跳过目标节点
    for (int i = 0; i < current_level; ++i) {
      if (update[i]->forward_[i] != current) {
        break;
      }
      update[i]->forward_[i] = current->forward_[i];
    }

    // 更新 backward 指针
    for (int i = 0; i < current->backward_.size() && i < current_level; ++i) {
      if (current->forward_[i]) {
        current->forward_[i]->set_backward(i, update[i]);
      }
    }

    // 更新跳表的内存大小
    size_bytes -= key.size() + current->value_.size() + sizeof(uint64_t);

    // 如果删除的节点是最高层的节点，更新跳表的当前层级
    while (current_level > 1 && head->forward_[current_level - 1] == nullptr) {
      current_level--;
    }
  }
}

// 刷盘时可以直接遍历最底层链表
std::vector<std::tuple<std::string, std::string, uint64_t>> SkipList::flush() {
  // std::shared_lock<std::shared_mutex> slock(rw_mutex);
  spdlog::debug("SkipList--flush(): Starting to flush skiplist data");

  std::vector<std::tuple<std::string, std::string, uint64_t>> data;
  auto node = head->forward_[0];
  while (node) {
    data.emplace_back(node->key_, node->value_, node->tranc_id_);
    node = node->forward_[0];
  }

  spdlog::debug("SkipList--flush(): Flushed {} entries", data.size());

  return data;
}

size_t SkipList::get_size() {
  // std::shared_lock<std::shared_mutex> slock(rw_mutex);
  return size_bytes;
}

// 清空跳表，释放内存
void SkipList::clear() {
  // std::unique_lock<std::shared_mutex> lock(rw_mutex);
  head = std::make_shared<SkipListNode>("", "", max_level, 0);
  size_bytes = 0;
}

SkipListIterator SkipList::begin() {
  // return SkipListIterator(head->forward[0], rw_mutex);
  return SkipListIterator(head->forward_[0]);
}

SkipListIterator SkipList::end() {
  return SkipListIterator(); // 使用空构造函数
}

// 找到前缀的起始位置
// 返回第一个前缀匹配或者大于前缀的迭代器
SkipListIterator SkipList::begin_preffix(const std::string &preffix) {
  // TODO: Lab1.3 任务：实现前缀查询的起始位置
  spdlog::trace("SkipList begin is preffix:'{}'",preffix);

  auto current = head;
  //首先从最高层中查询
  for(int i = current_level-1;i>=0;i--){
    while(current->forward_[i] && current->forward_[i]->key_<preffix){
      current = current->forward_[i];
    }
  }

  //移动到最底层
  current = current->forward_[0];
  if(current && current->key_==preffix){
    spdlog::trace("preffix {} matched with '{}'",preffix,current->key_);
  }

  return SkipListIterator(current);
}

// 找到前缀的终结位置
SkipListIterator SkipList::end_preffix(const std::string &prefix) {
  // TODO: Lab1.3 任务：实现前缀查询的终结位置

  auto current = head;
  //首先从最高层中查询
  for(int i = current_level-1;i>=0;i--){
    while(current->forward_[i] && current->forward_[i]->key_<prefix){
      current = current->forward_[i];
    }
  }

  //移动到最底层
  current = current->forward_[0];

  // 找到第一个前缀不是prefix的节点
  while(current && current->key_.substr(0,prefix.size()) == prefix){
    current = current->forward_[0];
  }

  if(current){
    spdlog::trace("preffix end at '{}'",prefix);
  } else {
    spdlog::trace("{} end at the skiplist",prefix);
  }

  return SkipListIterator(current);
}

// ? 这里单调谓词的含义是, 整个数据库只会有一段连续区间满足此谓词
// ? 例如之前特化的前缀查询，以及后续可能的范围查询，都可以转化为谓词查询
// ? 返回第一个满足谓词的位置和最后一个满足谓词的迭代器
// ? 如果不存在, 范围nullptr
// ? 谓词作用于key, 且保证满足谓词的结果只在一段连续的区间内, 例如前缀匹配的谓词
// ? predicate返回值:
// ?   0: 满足谓词
// ?   >0: 不满足谓词, 需要向右移动
// ?   <0: 不满足谓词, 需要向左移动
// ! Skiplist 中的谓词查询不会进行事务id的判断, 需要上层自己进行判断
std::optional<std::pair<SkipListIterator, SkipListIterator>>
SkipList::iters_monotony_predicate(
    std::function<int(const std::string &)> predicate) {
  // TODO: Lab1.3 任务：实现谓词查询的起始位置

  auto current = head;
  SkipListIterator begin_iter = SkipListIterator(nullptr);
  SkipListIterator end_iter = SkipListIterator(nullptr);

  // 用于判断是否找到了开始位置
  bool find_begin = false;

  //同样从最高层开始查询
  for(int i = current_level - 1; i >= 0; i--){
    while(!find_begin){
      auto forward_i = current->forward_[i];

      //如果当前节点的下一个节点为空指针，表示已经遍历到这一层的末尾，需要进行下一个层级的遍历
      if(forward_i == nullptr){
        break;
      }

      int flag = predicate(forward_i->key_);

      /*
        根据predicate返回值flag与0的关系判断是否需要移动
          1. =0表示正好满足要求
          2. >0表示向后移动（forward_）才能找到起始位置
          3. <0表示向前移动（backward_）才能找到起始位置，但是当前位置如果不是起始位置的话，再向前怎么移动也不会找到起始位置
      */
      if(flag == 0){
        // 找到了 这一层 的起始位置，设置为true，并记录下来
        find_begin = true;
        current = forward_i;
        break;
      } else if (flag > 0){
        current = forward_i;
      } else {
        break;
      }
    }
  }

  // 上述过程还是没有找到起始位置时，抛出问题，直接返回空即可
  if(!find_begin){
    spdlog::trace("iters_monotony_predicate function has no begin_iter");
    return std::nullopt;
  }

  // 从这一层的起始位置开始向后找末尾
  auto cur = current;
  
  //这一层的起始位置有可能不是真正的起始位置，因为这个是在高层级中找到的起始位置，节点直接跨越较大，所以还需要前向遍历
  // 这一层的层级数与forward_和backward_数组的大小有关，这里使用backward_更直观
  for(int i = current->backward_.size() - 1; i>=0; i--){
    while(1){
      //当前节点的前驱节点为空或者是头节点
      if(current->backward_[i].lock()==nullptr || current->backward_[i].lock()==head){
        break;
      }

      int flag = predicate(current->backward_[i].lock()->key_);
      if(flag==0){
        //当前节点的前驱节点满足谓词，需要再向前寻找满足谓词的节点
        current = current->backward_[i].lock();
        continue;
      } else if (flag > 0){
        //大于0表示，当前节点的前驱节点不是起始位置，起始位置在这个节点的后面，但是后面的节点就是current。出现这个问题的原因是高层级跨越了多个节点，所以需要降低层级继续寻找
        break;
      } else {
        //小于0表示，当前节点的前驱节点不是起始位置，还需要向前查询，这种情况是不符合逻辑的。当前位置满足谓词，前面的节点的返回值应该大于0
        spdlog::error("iters_predicate: invalid begin direction");
        throw std::runtime_error("iters_predicate: invalid begin direction");
      }
    }
  }

  //上述操作完成后找到了起始位置
  begin_iter = SkipListIterator(current);

  for(int i = cur->forward_.size()-1;i>=0;i--){
    while(1){
       if (cur->forward_[i] == nullptr) {
        // 当前层没有后向节点
        break;
      }

      int flag = predicate(cur->forward_[i]->key_);
      if(flag == 0){
        //等于0表示当前位置符合要求，继续向后检测
        cur = cur->forward_[i];
        continue;
      } else if(flag > 0){
        //需要向后移动，但是当前位置不满足谓词时，正常逻辑应该向前而不是向后，这种情况需要抛出错误
        spdlog::error("iters_predicate: invalid end direction");
        throw std::runtime_error("iters_predicate: invalid end direction");
      } else {
        //需要向前移动，也就是在这个层级上真正的结尾节点被跨过去了，所以前往低层级寻找
        break;
      }
    }
  }

  //找到了末尾，不过根据STL的设计，区间一般是左闭右开，所以需要++
  end_iter = SkipListIterator(cur);
  ++end_iter;

  spdlog::trace("SkipList--iters_monotony_predicate(): range found");
  
  return std::make_optional<std::pair<SkipListIterator, SkipListIterator>>(begin_iter,end_iter);
}

// ? 打印跳表, 你可以在出错时调用此函数进行调试
void SkipList::print_skiplist() {
  for (int level = 0; level < current_level; level++) {
    std::cout << "Level " << level << ": ";
    auto current = head->forward_[level];
    while (current) {
      std::cout << current->key_;
      current = current->forward_[level];
      if (current) {
        std::cout << " -> ";
      }
    }
    std::cout << std::endl;
  }
  std::cout << std::endl;
}
} // namespace tiny_lsm