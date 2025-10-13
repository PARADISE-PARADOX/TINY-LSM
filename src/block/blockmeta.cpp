#include "../../include/block/blockmeta.h"
#include <cstdint>
#include <cstring>
#include <functional>
#include <stdexcept>

namespace tiny_lsm {
BlockMeta::BlockMeta() : offset(0), first_key(""), last_key("") {}

BlockMeta::BlockMeta(size_t offset, const std::string &first_key,
                     const std::string &last_key)
    : offset(offset), first_key(first_key), last_key(last_key) {}

void BlockMeta::encode_meta_to_slice(std::vector<BlockMeta> &meta_entries,
                                     std::vector<uint8_t> &metadata) {
  // TODO: Lab 3.4 将内存中所有`Blcok`的元数据编码为二进制字节数组
  // ? 输入输出都由参数中的引用给定, 你不需要自己创建`vector`

  // 计算总体大小

  // 获取meta部分的数组元素个数
  uint32_t num_entries = meta_entries.size();
  size_t total_size =
      sizeof(uint32_t); //最起码有4字节表示meta数组的大小需要记录，即便没有元素

  //计算所有meta数组的占用字节大小
  for (const auto &meta : meta_entries) {
    total_size += sizeof(uint32_t) +      // offset
                  sizeof(uint16_t) +      // first_key len
                  meta.first_key.size() + // first_key
                  sizeof(uint16_t) +      // last_key len
                  meta.last_key.size();   // last_key
  }

  total_size += sizeof(uint32_t); // hash

  // 分配空间
  metadata.resize(total_size);
  uint8_t *ptr = metadata.data();

  // 1 写入meta数组的元素个数
  memcpy(ptr, &num_entries, sizeof(uint32_t));
  ptr += sizeof(uint32_t);

  // 写入meta数组
  for (const auto &meta : meta_entries) {
    // offset
    uint32_t offset32 = static_cast<uint32_t>(meta.offset);
    memcpy(ptr, &offset32, sizeof(uint32_t));
    ptr += sizeof(uint32_t);

    // first_key长度和first_key
    uint16_t first_key_len = meta.first_key.size();
    memcpy(ptr, &first_key_len, sizeof(uint16_t));
    ptr += sizeof(uint16_t);
    memcpy(ptr, meta.first_key.data(), first_key_len);
    ptr += first_key_len;

    // last_key的长度和last_key
    uint16_t last_key_len = meta.last_key.size();
    memcpy(ptr, &last_key_len, sizeof(uint16_t));
    ptr += sizeof(uint16_t);
    memcpy(ptr, meta.last_key.data(), last_key_len);
    ptr += last_key_len;
  }

  //最后写入hash,依据meta数组进行hash计算
  const uint8_t *meta_entries_start = metadata.data() + sizeof(uint32_t);
  const uint8_t *meta_entries_end = ptr;
  size_t meta_entries_len = meta_entries_end - meta_entries_start;

  auto hash_start = reinterpret_cast<const char *>(meta_entries_start);

  uint32_t hash_val = std::hash<std::string_view>{}(
      std::string_view(hash_start, meta_entries_len));

  memcpy(ptr, &hash_val, sizeof(uint32_t));
}

std::vector<BlockMeta>
BlockMeta::decode_meta_from_slice(const std::vector<uint8_t> &metadata) {
  // TODO: Lab 3.4 将二进制字节数组解码为内存中的`Blcok`元数据

  std::vector<BlockMeta> meta_entries;

  //首先判断编码后的内容大小，编码后的内容至少包含 num_entries(uint32_t) 和
  // hash(uint32_t)
  if (metadata.size() < 2 * sizeof(uint32_t)) {
    throw std::runtime_error("Size of metadata is invalid");
  }

  // 获取元素的个数
  const uint8_t *ptr = metadata.data();
  uint32_t num_entries;
  memcpy(&num_entries, ptr, sizeof(uint32_t));
  ptr += sizeof(uint32_t);

  //读取整个meta数组，其中一个元素占用了
  for (uint32_t offset_idx = 0; offset_idx < num_entries; ++offset_idx) {
    BlockMeta meta;

    // offset
    uint32_t offset32;
    memcpy(&offset32, ptr, sizeof(uint32_t));
    ptr += sizeof(uint32_t);
    meta.offset = offset32;

    // 所有内容中，字节数固定的使用memcpy即可，key本身（std::string）的大小是动态的，使用assign分配
    // first_key
    uint16_t first_key_len;
    memcpy(&first_key_len, ptr, sizeof(uint16_t));
    ptr += sizeof(uint16_t);
    meta.first_key.assign(reinterpret_cast<const char *>(ptr), first_key_len);
    ptr += first_key_len;

    // first_key
    uint16_t last_key_len;
    memcpy(&last_key_len, ptr, sizeof(uint16_t));
    ptr += sizeof(uint16_t);
    meta.last_key.assign(reinterpret_cast<const char *>(ptr), last_key_len);
    ptr += last_key_len;

    meta_entries.push_back(meta);
  }

  //验证hash
  uint32_t hash_val;
  memcpy(&hash_val, ptr, sizeof(uint32_t));

  const uint8_t *meta_entries_start = metadata.data() + sizeof(uint32_t);
  const uint8_t *meta_entries_end = ptr;
  size_t meta_entries_len = meta_entries_end - meta_entries_start;

  auto hash_start = reinterpret_cast<const char *>(meta_entries_start);

  uint32_t computed_hash = std::hash<std::string_view>{}(
      std::string_view(hash_start, meta_entries_len));

  if (hash_val != computed_hash) {
    throw std::runtime_error("Metadata hash mismatch");
  }

  return meta_entries;
}
} // namespace tiny_lsm