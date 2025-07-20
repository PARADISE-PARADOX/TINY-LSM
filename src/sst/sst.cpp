#include "../../include/sst/sst.h"
#include "../../include/config/config.h"
#include "../../include/consts.h"
#include "../../include/sst/sst_iterator.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>

namespace toni_lsm {

// **************************************************
// SST
// **************************************************

std::shared_ptr<SST> SST::open(size_t sst_id, FileObj file,
                               std::shared_ptr<BlockCache> block_cache) {
  // TODO Lab 3.6 打开一个SST文件, 返回一个描述类
  std::shared_ptr<SST> sst = std::make_shared<SST>();
  sst->sst_id = sst_id;
  sst->file = std::move(file);
  sst->block_cache = block_cache;

  size_t file_size = sst->file.size();

  // 文件大小小于extra时
  if(file_size < sizeof(uint64_t) * 2 + sizeof(uint32_t) * 2){
     throw std::runtime_error("SST size too small");
  }

  //读取最大最小的事务id
  auto min_tranc_id = sst->file.read_to_slice(file_size - sizeof(uint64_t) * 2, sizeof(uint64_t));
  auto max_tranc_id = sst->file.read_to_slice(file_size - sizeof(uint64_t), sizeof(uint64_t));
  
  memcpy(&sst->min_tranc_id_,min_tranc_id.data(),sizeof(uint64_t));
  memcpy(&sst->max_tranc_id_,max_tranc_id.data(),sizeof(uint64_t));

  //元数据块的偏移量和bloom的偏移量
  auto bloom_offset = sst->file.read_to_slice(file_size - sizeof(uint64_t) * 2 - sizeof(uint32_t),sizeof(uint32_t));
  auto meta_offset = sst->file.read_to_slice(file_size - sizeof(uint64_t) * 2 - sizeof(uint32_t) * 2, sizeof(uint32_t));

  memcpy(&sst->bloom_offset,bloom_offset.data(),sizeof(uint32_t));
  memcpy(&sst->meta_block_offset,meta_offset.data(),sizeof(uint32_t));

  size_t extra_size = sizeof(uint64_t) * 2 + sizeof(uint32_t) * 2;

  //读取bloom,当bloom section大小和extra section的大小 比文件大小要大时，表示存在bloom过滤器
  if(sst->bloom_offset + extra_size < file_size){
    uint32_t bloom_size = file_size - extra_size - sst->bloom_offset;

    auto bloom_section = sst->file.read_to_slice(sst->bloom_offset,bloom_size);
    auto bloom = BloomFilter::decode(bloom_section);
    sst->bloom_filter = std::make_shared<BloomFilter>(std::move(bloom));
  }

  //读取元数据块
  uint32_t meta_size =  sst->bloom_offset - sst->meta_block_offset;
  auto meta_section = sst->file.read_to_slice(sst->meta_block_offset, meta_size);
  sst->meta_entries = BlockMeta::decode_meta_from_slice(meta_section);

  //设置第一个和最后一个key
  if(!sst->meta_entries.empty()){
    sst->first_key = sst->meta_entries.front().first_key;
    sst->last_key = sst->meta_entries.back().last_key;
  }

  return sst;
}

void SST::del_sst() { file.del_file(); }

std::shared_ptr<SST> SST::create_sst_with_meta_only(
    size_t sst_id, size_t file_size, const std::string &first_key,
    const std::string &last_key, std::shared_ptr<BlockCache> block_cache) {
  auto sst = std::make_shared<SST>();
  sst->file.set_size(file_size);
  sst->sst_id = sst_id;
  sst->first_key = first_key;
  sst->last_key = last_key;
  sst->meta_block_offset = 0;
  sst->block_cache = block_cache;

  return sst;
}

std::shared_ptr<Block> SST::read_block(size_t block_idx) {
  // TODO: Lab 3.6 根据 block 的 id 读取一个 `Block`
  if(block_idx >= meta_entries.size()){
    throw std::runtime_error("block index out of range");
  }

  //缓存中查找
  if(block_cache != nullptr){
    auto cache_ptr = block_cache->get(this->sst_id,block_idx);
    if(cache_ptr != nullptr){
      return cache_ptr;
    }
  }else{
    throw std::runtime_error("Block cache is not exsist");
  }

  const auto &meta = meta_entries[block_idx];
  size_t block_size;

  if(block_idx == meta_entries.size() - 1){
    block_size = meta_block_offset - meta.offset;
  }else{
    block_size = meta_entries[block_idx + 1].offset - meta.offset;
  }

  auto block_data = file.read_to_slice(meta.offset, block_size);
  auto res = Block::decode(block_data,true);

  if(block_cache){
    block_cache->put(this->sst_id, block_idx, res);
  }else{
    throw std::runtime_error("Block cache is  exsisted, put failed");
  }

  return res;
}

size_t SST::find_block_idx(const std::string &key) {
  // 先在布隆过滤器判断key是否存在
  // TODO: Lab 3.6 二分查找
  // ? 给定一个 `key`, 返回其所属的 `block` 的索引
  // ? 如果没有找到包含该 `key` 的 Block，返回-1
  
   if (meta_entries.empty()) {
    return -1;
  }

  size_t left=0;
  size_t right = meta_entries.size() - 1;
  while(right>=left){
    size_t mid = left + (right - left) / 2;
    auto mid_meta = meta_entries[mid];
    if(mid_meta.first_key > key){
      right = mid -1;
    } else if(mid_meta.last_key < key){
      left = mid + 1;
    }else{
      return mid;
    }
  }
  return -1;
}

SstIterator SST::get(const std::string &key, uint64_t tranc_id) {
  // TODO: Lab 3.6 根据查询`key`返回一个迭代器
  // ? 如果`key`不存在, 返回一个无效的迭代器即可
  if (key < first_key || key > last_key) {
    return this->end();
  }

  // 在布隆过滤器判断key是否存在
  if (bloom_filter != nullptr && !bloom_filter->possibly_contains(key)) {
    return this->end();
  }

  return SstIterator(shared_from_this(), key, tranc_id);
}

size_t SST::num_blocks() const { return meta_entries.size(); }

std::string SST::get_first_key() const { return first_key; }

std::string SST::get_last_key() const { return last_key; }

size_t SST::sst_size() const { return file.size(); }

size_t SST::get_sst_id() const { return sst_id; }

SstIterator SST::begin(uint64_t tranc_id) {
  // TODO: Lab 3.6 返回起始位置迭代器
  if(meta_entries.empty()){
    return this->end();
  }

  return SstIterator(shared_from_this(), tranc_id);
}

SstIterator SST::end() {
  // TODO: Lab 3.6 返回终止位置迭代器
  SstIterator res(shared_from_this(), 0);
  res.m_block_idx = meta_entries.size();
  res.m_block_it = nullptr;
  return res;
}

std::pair<uint64_t, uint64_t> SST::get_tranc_id_range() const {
  return std::make_pair(min_tranc_id_, max_tranc_id_);
}

// **************************************************
// SSTBuilder
// **************************************************

SSTBuilder::SSTBuilder(size_t block_size, bool has_bloom) : block(block_size), block_size(block_size) {
  // 初始化第一个block
  if (has_bloom) {
    bloom_filter = std::make_shared<BloomFilter>(
        TomlConfig::getInstance().getBloomFilterExpectedSize(),
        TomlConfig::getInstance().getBloomFilterExpectedErrorRate());
  }
  meta_entries.clear();
  data.clear();
  first_key.clear();
  last_key.clear();
}

void SSTBuilder::add(const std::string &key, const std::string &value,
                     uint64_t tranc_id) {
  if(first_key.empty()){
    first_key = key;
  }

  if(bloom_filter != nullptr){
    bloom_filter->add(key);
  }

  max_tranc_id_ = std::max(tranc_id,max_tranc_id_);
  min_tranc_id_ = std::min(tranc_id,min_tranc_id_);

  // 计算当前block已使用的大小
  size_t current_block_size = block.data.size() + block.offsets.size() * sizeof(uint16_t);
  
  // 计算新entry需要的大小
  size_t entry_size = sizeof(uint16_t) + key.size() + sizeof(uint16_t) + value.size() + sizeof(uint64_t);
  size_t offset_size = sizeof(uint16_t);
  size_t total_new_size = current_block_size + entry_size + offset_size;

  bool force_write = key==last_key;

  // 如果当前block已满或即将满，完成当前block并创建新的block
  if(!force_write && total_new_size > block_size) {
    finish_block();
    block = Block(block_size);
  }

  // 添加entry
  if(!block.add_entry(key, value, tranc_id, force_write || total_new_size <= block_size)){
    throw std::runtime_error("Failed to add entry even with force write");
  }
  
  last_key = key;
}

size_t SSTBuilder::estimated_size() const { return data.size(); }

void SSTBuilder::finish_block() {
  // TODO: Lab 3.5 构建块
  // ? 当 add
  // 函数发现当前的`block`容量超出阈值时，需要将其编码到`data`，并清空`block`
  Block old_block = std::move(this->block);
  
  // 获取当前 block 的第一个和最后一个键
  std::string block_first_key = old_block.get_first_key();
  std::string block_last_key = last_key;
  
  // 先获取block的编码数据，因为move之后old_block就不能用了
  auto encoded_block = old_block.encode();
  
  meta_entries.emplace_back(data.size(), block_first_key, block_last_key);

  uint32_t block_hash = static_cast<uint32_t>(std::hash<std::string_view>{}(
      std::string_view(reinterpret_cast<const char *>(encoded_block.data()),
                       encoded_block.size())));

  size_t original_size = data.size();
  data.resize(original_size + encoded_block.size() + sizeof(uint32_t));
  
  memcpy(data.data() + original_size, encoded_block.data(), encoded_block.size());
  memcpy(data.data() + original_size + encoded_block.size(), &block_hash, sizeof(uint32_t));                
}

std::shared_ptr<SST>
SSTBuilder::build(size_t sst_id, const std::string &path,
                  std::shared_ptr<BlockCache> block_cache) {
  // TODO 3.5 构建一个SST
  // 完成最后一个block
  if (!block.is_empty()) {
    finish_block();
  }

  if(meta_entries.empty()){
    throw std::runtime_error("no data to build SST");
  }

  // 构建meta section
  std::vector<uint8_t> meta_section;
  BlockMeta::encode_meta_to_slice(meta_entries,meta_section);

  //偏移量
  uint32_t meta_offset = data.size();

  //构建完整的文件内容，包含： block部分，meta部分，bloom部分，额外部分
  std::vector<uint8_t> file_content;
  file_content = std::move(data);
  file_content.insert(file_content.end(),meta_section.begin(),meta_section.end());

  //构建bloom filter
  uint32_t bloom_offset = file_content.size();
  if(bloom_filter != nullptr){
    std::vector<uint8_t> bloom_section = bloom_filter->encode();
    file_content.insert(file_content.end(),bloom_section.begin(),bloom_section.end());
  }

  auto extra_len = sizeof(uint32_t) * 2 + sizeof(uint64_t) * 2;
  std::vector<uint8_t> extra_section;
  // extra_section.emplace_back(meta_offset);
  // extra_section.emplace_back(bloom_offset);
  // extra_section.emplace_back(min_tranc_id_);
  // extra_section.emplace_back(max_tranc_id_);
  extra_section.resize(extra_len);
  uint8_t *ptr = extra_section.data();
  
  // 添加元数据块偏移量
  memcpy(ptr,&meta_offset,sizeof(uint32_t));
  ptr += sizeof(uint32_t);

  //bloom偏移量
  memcpy(ptr,&bloom_offset,sizeof(uint32_t));
  ptr += sizeof(uint32_t);

  //最大最小事务id
  memcpy(ptr,&min_tranc_id_,sizeof(uint64_t));
  ptr += sizeof(uint64_t);

  memcpy(ptr,&max_tranc_id_,sizeof(uint64_t));
  ptr += sizeof(uint64_t);

  file_content.insert(file_content.end(),extra_section.begin(),extra_section.end());

  // 创建文件
  FileObj file = FileObj::create_and_write(path, file_content);

  std::shared_ptr<SST> res = std::make_shared<SST>();

  res->file = std::move(file);


  res->bloom_offset = bloom_offset;
  res->meta_block_offset = meta_offset;
  res->sst_id = sst_id;
  res->first_key = first_key;
  res->last_key = last_key;
  res->meta_entries = std::move(meta_entries);
  res->bloom_filter = this->bloom_filter;
  res->block_cache = block_cache;
  res->min_tranc_id_ = min_tranc_id_;
  res->max_tranc_id_ = max_tranc_id_;


  return res;
}
} // namespace toni_lsm