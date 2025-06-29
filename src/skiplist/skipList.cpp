#include "../../include/skiplist/skiplist.h"
#include <cstddef>
#include <cstdint>
#include <format>
#include <iostream>
#include <memory>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace toni_lsm {

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
  if(other.get_type()!=this->get_type()){
    return false;
  }

  //从基类向派生类进行类型转换时，使用静态转换不安全，所以使用dynamic_cast动态转换
  auto skipOther = dynamic_cast<const SkipListIterator &>(other);
  return current == skipOther.current;
}

bool SkipListIterator::operator!=(const BaseIterator &other) const {
  // TODO: Lab1.2 任务：实现SkipListIterator的!=操作符
   return !(*this == other);
}

SkipListIterator::value_type SkipListIterator::operator*() const {
  // TODO: Lab1.2 任务：实现SkipListIterator的*操作符
  if(!current){
    throw std::runtime_error("invalid iterator");
  }
  return {current->key_,current->value_};
}

IteratorType SkipListIterator::get_type() const {
  // TODO: Lab1.2 任务：实现SkipListIterator的get_type
  // ? 主要是为了熟悉基类的定义和继承关系
  return IteratorType::SkipListIterator;
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
  int level= 1;
  while (dis_01(gen) && level < max_level) {
    level++;
  }
  return level;
}

// 插入或更新键值对
void SkipList::put(const std::string &key, const std::string &value,
                   uint64_t tranc_id) {
  spdlog::trace("SkipList--put({}, {}, {})", key, value, tranc_id);

  // TODO: Lab1.1  任务：实现插入或更新键值对
  // ? Hint: 你需要保证不同`Level`的步长从底层到高层逐渐增加
  // ? 你可能需要使用到`random_level`函数以确定层数, 其注释中为你提供一种思路
  // ? tranc_id 为事务id, 现在你不需要关注它, 直接将其传递到 SkipListNode 的构造函数中即可


  //记录每一层需要更新的前驱节点的位置，数组的索引为层级数，update[1]表示第一层级的前驱节点，要插入的节点就在前驱节点的后面
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
  // spdlog::trace("SkipList--get({}) called", key);
  // ? 你可以参照上面的注释完成日志输出以便于调试
  // ? 日志为输出到你执行二进制所在目录下的log文件夹

  // TODO: Lab1.1 任务：实现查找键值对,
  // TODO: 并且你后续需要额外实现SkipListIterator中的TODO部分(Lab1.2)
  spdlog::trace("SkipList--get({}) called", key);
  auto current = head;
  // 从最高层开始查找
  for (int i = current_level - 1; i >= 0; i--) {
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
  // TODO: Lab1.1 任务：实现删除键值对
  std::vector<std::shared_ptr<SkipListNode>> update(max_level, nullptr);

  // std::unique_lock<std::shared_mutex> lock(rw_mutex);
  auto current = head;

  // 从最高层开始查找目标节点
  for (int i = current->forward_.size() - 1; i >= 0; i--) {
    while (current->forward_[i] && current->forward_[i]->key_ < key) {
      current = current->forward_[i];
    }
    update[i] = current;
  }

  // 移动到最底层
  current = current->forward_[0];

  //如果找到了目标的节点，执行删除操作
  if(current && current->key_ == key){
    // 更新每一层的forward指针，并跳过目标的节点。
    for(int i=0;i<current_level;i++){
      //如果这一层级的前驱节点指向的节点不是当前节点，就不满足要求，直接break
      if(update[i]->forward_[i] != current){
        break;
      }
      //更新这一层级的前驱节点的forward指针
       /*
        Level 0: head -> A -> B -> C -> D -> E 为例
        删除节点B：update[i]->forward_[i]为B,current->forward_[i]为C
        实现的效果就是 A -> C
      */
      update[i]->forward_[i] = current->forward_[i];
    }

    //更新backward指针
    for(int i=0;i<current->backward_.size() && i < current_level; i++){
      if(current->forward_[i]){
        /*
          Level 0: head -> A -> B -> C -> D -> E 为例
          要删除节点B，current->forward_[i]就是C，将C的backward_[i]设为update[i]，也就是current的前一个节点A。
          实现的效果就是 head -> A -> C -> D -> E
        */ 
        current->forward_[i]->set_backward(i, update[i]);
      }
    }

    //更新跳表的内存大小
    size_bytes -= key.size() + current->value_.size() + sizeof(uint64_t);

    //如果删除的节点在最高层，更新跳表的当前层级
    while(current_level>1 && head->forward_[current_level-1] == nullptr){
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
  spdlog::trace("SkipList--begin_preffix('{}') called", preffix);
  auto current = head;
  for(int i=current_level-1;i>=0;i--){
    while(current->forward_[i] && current->forward_[i]->key_<preffix){
      current = current->forward_[i];
    }
  }

  current = current->forward_[0];

  //current存在且key值与前缀相等的时候
  if (current && current->key_.substr(0, preffix.size()) == preffix) {
    spdlog::trace("SkipList--begin_preffix('{}'): first match at '{}'", preffix,
                  current->key_);
  }

  return SkipListIterator(current);
}

// 找到前缀的终结位置
SkipListIterator SkipList::end_preffix(const std::string &prefix) {
  // TODO: Lab1.3 任务：实现前缀查询的终结位置
  spdlog::trace("SkipList--end_preffix('{}') called",prefix);
  auto current = head;

  for(int i=current_level-1;i>=0;i--){
    while(current->forward_[i] && current->forward_[i]->key_<prefix){
      current = current->forward_[i];
    }
  }

  current = current->forward_[0];

  while (current && current->key_.substr(0, prefix.size()) == prefix) {
    current = current->forward_[0];
  }

  if (current) {
    spdlog::trace("SkipList--begin_preffix('{}'): end at '{}'", prefix,
                  current->key_);
  } else {
    spdlog::trace("SkipList--begin_preffix('{}'): end at the skiplist end",
                  prefix);
  }

  // 返回当前节点的迭代器
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

  SkipListIterator startIterator = SkipListIterator();//满足要求的第一个节点的迭代器
  SkipListIterator endIterator = SkipListIterator();//满足要求的最后一个节点的迭代器

  bool find = false;
  for(int i=current_level-1;i>=0;i--){
    while(current->forward_[i]){
      //记录当前节点的下一个节点
      // auto next_i = current->forward_[i];
      if(current->forward_[i] == nullptr){
        break;
      }
      int flag = predicate(current->forward_[i]->key_);
      if(flag==0){ //flag为0表示满足谓词
        current = current->forward_[i];
        find = true;
        break;
      }else if(flag>0){ //大于0，需要继续向后移动
        current = current->forward_[i];
      }else{ 
        break;
      }
    }
    if(find){
      break;
    }
  }
  if(!find){
    spdlog::trace("SkipList--iters_monotony_predicate() unmatch");
    return std::nullopt;
  }

  auto new_current = current;
  //current目前是一个满足要求的迭代器，现在需要找到第一个满足要求的迭代器
  //从最后一个节点所在的层级开始向前遍历，使用backward_
  for(int i=current->backward_.size()-1;i>=0;i--){
    //前面的节点是头结点或者为空时，前往下一层去寻找
    while(1){
      if(! current->backward_[i].lock()|| current->backward_[i].lock() == head ){
        break;
      }
      int flag = predicate(current->backward_[i].lock()->key_);
      if(flag==0){ //谓语满足时，继续向前遍历
        current = current->backward_[i].lock();
        continue;
      }else if(flag>0){ //大于0表示需要向后移动，但是向后移动无法找到满足条件的节点，直接跳出循环
        break;
      }else{ //小于0表示不满足，并继续向前移动，但是实际上应该是向后移动才能找到满足的节点，这种情况是系统的异常情况，需要抛出错误
        spdlog::error("iters_predicate: invalid flag");
        throw std::logic_error("cannot move to front");
      }
    }
  }

  //第一个迭代器
  startIterator = SkipListIterator(current);

  // 找到最后一个满足谓词的节点
  for (int i = new_current->forward_.size() - 1; i >= 0; i--) {
    while (1) {
      if (new_current->forward_[i] == nullptr) {
        // 当前层没有后向节点
        break;
      }
      int flag = predicate(new_current->forward_[i]->key_);
      if (flag == 0) {
        // 后一个位置满足谓词, 继续判断
        new_current = new_current->forward_[i];
        continue;
      } else if (flag < 0) {
        // 后一个位置不满足谓词
        // 需要尝试更小的步长(层级)
        break;
      } else {
        // 因为当前位置满足了谓词, 后一个位置不可能返回1
        // 这种情况属于跳表实现错误, 需要排查

        spdlog::error("iters_predicate: invalid direction");

        throw std::runtime_error("iters_predicate: invalid direction");
      }
    }
  }
  endIterator = SkipListIterator(new_current);

  //开区间
  ++endIterator;
  spdlog::trace("find range(startIterator,endIterator)");

  return std::make_optional<std::pair<SkipListIterator, SkipListIterator>>(startIterator,endIterator);
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
} // namespace toni_lsm