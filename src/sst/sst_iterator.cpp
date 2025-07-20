#include "../../include/sst/sst_iterator.h"
#include "../../include/sst/sst.h"
#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>

namespace toni_lsm {

// predicate返回值:
//   0: 谓词
//   >0: 不满足谓词, 需要向右移动
//   <0: 不满足谓词, 需要向左移动
std::optional<std::pair<SstIterator, SstIterator>> sst_iters_monotony_predicate(
    std::shared_ptr<SST> sst, uint64_t tranc_id,
    std::function<int(const std::string &)> predicate) {
  std::optional<SstIterator> final_begin = std::nullopt;
  std::optional<SstIterator> final_end = std::nullopt;
  
  // 先找到第一个可能包含满足谓词的block
  size_t start_block = 0;
  for (; start_block < sst->meta_entries.size(); start_block++) {
    BlockMeta &meta = sst->meta_entries[start_block];
    int first_flag = predicate(meta.first_key);
    int last_flag = predicate(meta.last_key);
    
    if (first_flag > 0) {
      continue;  // block的key都太小，继续向后找
    }
    if (first_flag <= 0) {
      break;  // 找到可能包含满足谓词的block
    }
  }
  
  if (start_block >= sst->meta_entries.size()) {
    return std::nullopt;
  }
  
  // 从这个block开始向后扫描，找到完整的范围
  for (size_t block_idx = start_block; block_idx < sst->meta_entries.size(); block_idx++) {
    BlockMeta &meta_i = sst->meta_entries[block_idx];
    int first_flag = predicate(meta_i.first_key);
    int last_flag = predicate(meta_i.last_key);
    
    if (first_flag > 0) {
      continue;  // block的key都太小，继续向后找
    }
    if (last_flag < 0) {
      break;  // block的key都太大，结束查找
    }
    
    auto block = sst->read_block(block_idx);
    auto result_i = block->get_monotony_predicate_iters(tranc_id, predicate);
    if (result_i.has_value()) {
      auto [i_begin, i_end] = result_i.value();
      if (!final_begin.has_value()) {
        auto tmp_it = SstIterator(sst, tranc_id);
        tmp_it.set_block_idx(block_idx);
        tmp_it.set_block_it(i_begin);
        final_begin = tmp_it;
      }
      
      auto tmp_it = SstIterator(sst, tranc_id);
      tmp_it.set_block_idx(block_idx);
      tmp_it.set_block_it(i_end);
      final_end = tmp_it;
    }
  }
  
  if (!final_begin.has_value() || !final_end.has_value()) {
    return std::nullopt;
  }
  
  return std::make_pair(*final_begin, *final_end);
}

SstIterator::SstIterator(std::shared_ptr<SST> sst, uint64_t tranc_id)
    : m_sst(sst), m_block_idx(0), m_block_it(nullptr), max_tranc_id_(tranc_id) {
  if (m_sst) {
    seek_first();
  }
}

SstIterator::SstIterator(std::shared_ptr<SST> sst, const std::string &key,
                         uint64_t tranc_id)
    : m_sst(sst), m_block_idx(0), m_block_it(nullptr), max_tranc_id_(tranc_id) {
  if (m_sst) {
    seek(key);
  }
}

void SstIterator::set_block_idx(size_t idx) { m_block_idx = idx; }
void SstIterator::set_block_it(std::shared_ptr<BlockIterator> it) {
  m_block_it = it;
}

void SstIterator::seek_first() {
  // TODO: Lab 3.6 将迭代器定位到第一个key
  if(!m_sst || m_sst->num_blocks() == 0){
    return;
  }

  m_block_idx = 0;
  auto m_block = m_sst->read_block(m_block_idx);
  m_block_it =std::make_shared<BlockIterator>(m_block,m_block_idx,max_tranc_id_);
}

void SstIterator::seek(const std::string &key) {
  // TODO: Lab 3.6 将迭代器定位到指定key的位置
  if(!m_sst){
    m_block_it = nullptr;
    return;
  }

  try{
    m_block_idx = m_sst->find_block_idx(key);

    //idx不在blocks的索引内时，迭代器置空，索引指向最后
    if(m_block_idx == -1 || m_block_idx >= m_sst->num_blocks()){
      m_block_it = nullptr;
      m_block_idx = m_sst->num_blocks();
      return;
    }
    auto block = m_sst->read_block(m_block_idx);
    if(!block){
      m_block_it = nullptr;
      return;
    }

    m_block_it = std::make_shared<BlockIterator>(block,key,max_tranc_id_);

    if(m_block_it->is_end()){
      m_block_idx = m_sst->num_blocks();
      m_block_it = nullptr;
      return;
    }
  } catch(const std::exception &) {
    m_block_it = nullptr;
    return;
  }
}

std::string SstIterator::key() {
  if (!m_block_it) {
    throw std::runtime_error("Iterator is invalid");
  }
  return (*m_block_it)->first;
}

std::string SstIterator::value() {
  if (!m_block_it) {
    throw std::runtime_error("Iterator is invalid");
  }
  return (*m_block_it)->second;
}

BaseIterator &SstIterator::operator++() {
  // TODO: Lab 3.6 实现迭代器自增
  if(!m_block_it){
    return *this;
  }

  ++(*m_block_it);

  if(m_block_it->is_end()){
    m_block_idx++;
    if(m_block_idx<m_sst->num_blocks()){
      //读取下一个block
      auto next_block = m_sst->read_block(m_block_idx);
      BlockIterator new_block_it(next_block,0,max_tranc_id_);
      (*m_block_it) = new_block_it;
    } else{
      m_block_it = nullptr;
    }
  }

  return *this;
}

bool SstIterator::operator==(const BaseIterator &other) const {
  // TODO: Lab 3.6 实现迭代器比较

  if(other.get_type() != IteratorType::SstIterator){
    return false;
  }

  auto other_sst_it = dynamic_cast<const SstIterator &>(other);

  if(!m_block_it || !other_sst_it.m_block_it){
    return false;
  }
  if(!m_block_it && !other_sst_it.m_block_it){
    return true;
  }

  if(m_sst!=other_sst_it.m_sst || m_block_idx!=other_sst_it.m_block_idx){
    return false;
  }

  return m_block_it==other_sst_it.m_block_it;
}

bool SstIterator::operator!=(const BaseIterator &other) const {
  // TODO: Lab 3.6 实现迭代器比较
  return !(*this==other);
}

SstIterator::value_type SstIterator::operator*() const {
  // TODO: Lab 3.6 实现迭代器解引用
  if(!m_block_it){
    throw std::runtime_error("invalid iterator");
  }

  auto res = **m_block_it;
  return res;
}

IteratorType SstIterator::get_type() const { return IteratorType::SstIterator; }

uint64_t SstIterator::get_tranc_id() const { return max_tranc_id_; }
bool SstIterator::is_end() const { return !m_block_it; }

bool SstIterator::is_valid() const {
  return m_block_it && !m_block_it->is_end() &&
         m_block_idx < m_sst->num_blocks();
}
SstIterator::pointer SstIterator::operator->() const {
  update_current();
  return &(*cached_value);
}

void SstIterator::update_current() const {
  if (!cached_value && m_block_it && !m_block_it->is_end()) {
    cached_value = *(*m_block_it);
  }
}

std::pair<HeapIterator, HeapIterator>
SstIterator::merge_sst_iterator(std::vector<SstIterator> iter_vec,
                                uint64_t tranc_id) {
  if (iter_vec.empty()) {
    return std::make_pair(HeapIterator(), HeapIterator());
  }

  HeapIterator it_begin;
  for (auto &iter : iter_vec) {
    while (iter.is_valid() && !iter.is_end()) {
      it_begin.items.emplace(
          iter.key(), iter.value(), -iter.m_sst->get_sst_id(), 0,
          tranc_id); // ! 此处的level暂时没有作用, 都作用于同一层的比较
      ++iter;
    }
  }
  return std::make_pair(it_begin, HeapIterator());
}
} // namespace toni_lsm