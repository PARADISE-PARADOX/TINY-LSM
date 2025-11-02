// include/utils/bloom_filter.cpp

#include "../..//include/utils/bloom_filter.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <numeric>
#include <string>
#include <cmath>

namespace tiny_lsm {

BloomFilter::BloomFilter() {};

// 构造函数，初始化布隆过滤器
// expected_elements: 预期插入的元素数量
// false_positive_rate: 允许的假阳性率
BloomFilter::BloomFilter(size_t expected_elements, double false_positive_rate)
    : expected_elements_(expected_elements),
      false_positive_rate_(false_positive_rate) {
  // TODO: Lab 4.9: 初始化数组长度
  double formula1 = std::log(false_positive_rate) * expected_elements;
  double formula2 = std::pow((std::log(2)), 2);
  double m = -(formula1 / formula2);

  // 计算位数组的长度，向上取整
  num_bits_ = static_cast<size_t>(std::ceil(m));

   // 计算哈希函数的数量
  double k = m * std::log(2) / expected_elements;
  num_hashes_ = static_cast<size_t>(std::ceil(k));

  bits_.resize(num_bits_, false);
}

void BloomFilter::add(const std::string &key) {
  // TODO: Lab 4.9: 添加一个记录到布隆过滤器中
  for(size_t i = 0; i < num_hashes_; i++){
    bits_[hash(key, i)] = true;
  }
}

//  如果key可能存在于布隆过滤器中，返回true；否则返回false
bool BloomFilter::possibly_contains(const std::string &key) const {
  // TODO: Lab 4.9: 检查一个记录是否可能存在于布隆过滤器中
  for(size_t i = 0; i < num_hashes_; i++){
    auto idx = hash(key,i);
    if(!bits_[idx]) {
      return false;
    }
  }
  return true;
}

// 清空布隆过滤器
void BloomFilter::clear() { bits_.assign(bits_.size(), false); }

size_t BloomFilter::hash1(const std::string &key) const {
  std::hash<std::string> hasher;
  return hasher(key);
}

size_t BloomFilter::hash2(const std::string &key) const {
  std::hash<std::string> hasher;
  return hasher(key + "salt");
}

size_t BloomFilter::hash(const std::string &key, size_t idx) const {
  // TODO: Lab 4.9: 计算哈希值
  // ? idx 标识这是第几个哈希函数
  // ? 你需要按照某些方式, 从 hash1 和 hash2 中组合成新的哈希函数
  auto h1 = hash1(key);
  auto h2 = hash2(key);
  return (h1 + idx * h2) % num_bits_;
}

// 编码布隆过滤器为 std::vector<uint8_t>
std::vector<uint8_t> BloomFilter::encode() {
  // TODO: Lab 4.9: 编码布隆过滤器

  std::vector<uint8_t> encoded_bloom;

  // 编码 expected_elements_
  encoded_bloom.insert(encoded_bloom.end(),
              reinterpret_cast<const uint8_t *>(&expected_elements_),
              reinterpret_cast<const uint8_t *>(&expected_elements_) +
                  sizeof(expected_elements_));

  // 编码 false_positive_rate_
  encoded_bloom.insert(encoded_bloom.end(),
              reinterpret_cast<const uint8_t *>(&false_positive_rate_),
              reinterpret_cast<const uint8_t *>(&false_positive_rate_) +
                  sizeof(false_positive_rate_));

  // 编码 num_bits_
  encoded_bloom.insert(encoded_bloom.end(), reinterpret_cast<const uint8_t *>(&num_bits_),
              reinterpret_cast<const uint8_t *>(&num_bits_) +
                  sizeof(num_bits_));

  // 编码 num_hashes_
  encoded_bloom.insert(encoded_bloom.end(), reinterpret_cast<const uint8_t *>(&num_hashes_),
              reinterpret_cast<const uint8_t *>(&num_hashes_) +
                  sizeof(num_hashes_));

  // 编码 bits_
  // 向上取整对其字节
  size_t num_bytes = (num_bits_ + 7) / 8;
  for(size_t i = 0; i < num_bytes ;i++){
    uint8_t byte = 0;
    for(size_t j=0; j < 8; j++){
      if(i * 8 + j < num_bits_){
        byte |= (bits_[i * 8 + j] << j);
      }
    }
    encoded_bloom.push_back(byte);
  }

  return encoded_bloom;
}

// 从 std::vector<uint8_t> 解码布隆过滤器
BloomFilter BloomFilter::decode(const std::vector<uint8_t> &data) {
  // TODO: Lab 4.9: 解码布隆过滤器
  size_t index = 0;

   // 解码 expected_elements_
  size_t expected_elements;
  std::memcpy(&expected_elements, &data[index], sizeof(expected_elements));
  index += sizeof(expected_elements);

   // 解码 false_positive_rate_
  double false_positive_rate;
  std::memcpy(&false_positive_rate, &data[index], sizeof(false_positive_rate));
  index += sizeof(false_positive_rate);

  // 解码 num_bits_
  size_t num_bits;
  std::memcpy(&num_bits, &data[index], sizeof(num_bits));
  index += sizeof(num_bits);

  // 解码 num_hashes_
  size_t num_hashes;
  std::memcpy(&num_hashes, &data[index], sizeof(num_hashes));
  index += sizeof(num_hashes);

  // 解码 bits_
  std::vector<bool> bits(num_bits, false);
  size_t num_bytes = (num_bits + 7) / 8;
  for (size_t i = 0; i < num_bytes; ++i) {
    uint8_t byte = data[index++];
    for (size_t j = 0; j < 8; ++j) {
      if (i * 8 + j < num_bits) {
        bits[i * 8 + j] = (byte >> j) & 1;
      }
    }
  }

  BloomFilter bf;
  bf.expected_elements_ = expected_elements;
  bf.false_positive_rate_ = false_positive_rate;
  bf.num_bits_ = num_bits;
  bf.num_hashes_ = num_hashes;
  bf.bits_ = bits;

  return bf;
}
} // namespace tiny_lsm