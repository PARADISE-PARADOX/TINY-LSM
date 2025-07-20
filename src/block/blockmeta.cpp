#include "../../include/block/blockmeta.h"
#include <cstdint>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string_view>
#include <sys/types.h>

namespace toni_lsm {
BlockMeta::BlockMeta() : offset(0), first_key(""), last_key("") {}

BlockMeta::BlockMeta(size_t offset, const std::string &first_key,
                     const std::string &last_key)
    : offset(offset), first_key(first_key), last_key(last_key) {}

void BlockMeta::encode_meta_to_slice(std::vector<BlockMeta> &meta_entries,
                                     std::vector<uint8_t> &metadata) {
  // TODO: Lab 3.4 将内存中所有`Blcok`的元数据编码为二进制字节数组
  // ? 输入输出都由参数中的引用给定, 你不需要自己创建`vector`
  
  //每一个meta开头都有一个uint32_t的num表示Meta个数
  size_t total_size = sizeof(uint32_t); 

  for(const auto &meta:meta_entries){
    total_size += sizeof(uint32_t) // offset
                + sizeof(uint16_t) //first key的长度的大小
                + meta.first_key.size() // first key的大小
                + sizeof(uint16_t) //last key的长度的大小
                + meta.last_key.size(); // last key的大小
  }

  total_size += sizeof(uint32_t); //hash大小为4字节

  //metadata用于存储编码后的数据
  metadata.resize(total_size);
  uint8_t *ptr = metadata.data(); //指向metadata的指针

  //写入元素个数num
  auto meta_entries_size = meta_entries.size();
  memcpy(ptr,&meta_entries_size,sizeof(uint32_t));
  ptr += sizeof(uint32_t);
  
  //写入entry的内容
  for (const auto &meta : meta_entries) {

    //写入offset，头文件中的类型为size_t，需要类型转换
    uint32_t encode_offset = static_cast<uint32_t>(meta.offset);
    memcpy(ptr,&encode_offset,sizeof(uint32_t));
    ptr += sizeof(uint32_t);

    //写入first key
    uint16_t first_key_len = meta.first_key.size();
    memcpy(ptr,&first_key_len,sizeof(uint16_t));
    ptr += sizeof(uint16_t);
    memcpy(ptr,meta.first_key.data(),first_key_len);
    ptr += first_key_len;


    //写入last key
    uint16_t last_key_len = meta.last_key.size();
    memcpy(ptr,&last_key_len,sizeof(uint16_t));
    ptr += sizeof(uint16_t);
    memcpy(ptr,meta.last_key.data(),last_key_len);
    ptr += last_key_len;

  }

  //需要计算hash的部分需要去除开头的num
  const uint8_t *data_start = metadata.data() + sizeof(uint32_t);

  uint32_t meta_hash = std::hash<std::string_view>{}(
    std::string_view(reinterpret_cast<const char *>(data_start),ptr-data_start)
  );

  memcpy(ptr,&meta_hash,sizeof(uint32_t));
}

std::vector<BlockMeta>
BlockMeta::decode_meta_from_slice(const std::vector<uint8_t> &metadata) {
  // TODO: Lab 3.4 将二进制字节数组解码为内存中的`Blcok`元数据
  std::vector<BlockMeta> meta_entries;

  // metadata的大小至少包含一个uint32_t的num和uint32_t的offset
  if(metadata.size() < sizeof(uint32_t) * 2){
    throw std::runtime_error("metadata size is less than 8 bytes");
  }

  //获取num
  uint32_t num;
  const uint8_t *ptr = metadata.data();
  memcpy(&num,ptr,sizeof(uint32_t));
  ptr += sizeof(uint32_t);

  //获取entries
  for(uint32_t i = 0 ;i < num ;i++){
    BlockMeta meta;

    //获取offset
    uint32_t offset;
    memcpy(&offset,ptr,sizeof(uint32_t));
    meta.offset = offset;
    ptr += sizeof(uint32_t);

    //获取firstkey
    uint16_t first_key_len;
    memcpy(&first_key_len,ptr,sizeof(uint16_t));
    ptr += sizeof(uint16_t);
    meta.first_key.assign(reinterpret_cast<const char *>(ptr),first_key_len);
    ptr += first_key_len;

    //获取lastkey
    uint16_t last_key_len;
    memcpy(&last_key_len,ptr,sizeof(uint16_t));
    ptr += sizeof(uint16_t);
    meta.last_key.assign(reinterpret_cast<const char *>(ptr),last_key_len);
    ptr += last_key_len;

    meta_entries.emplace_back(meta);
  }

  //验证hash值是否一致
  uint32_t meta_hash;
  memcpy(&meta_hash,ptr,sizeof(uint32_t));

  //需要计算hash的部分需要去除开头的num
  const uint8_t *data_start = metadata.data() + sizeof(uint32_t);

  uint32_t expected_hash = std::hash<std::string_view>{}(
    std::string_view(reinterpret_cast<const char *>(data_start),ptr - data_start)
  );
  
  if(expected_hash!=meta_hash){
    throw std::runtime_error("meta hash not match");
  }

  return meta_entries;
}
} // namespace toni_lsm