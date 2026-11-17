//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// extendible_hash_table.cpp
//
// Identification: src/container/hash/extendible_hash_table.cpp
//
// Copyright (c) 2022, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <functional>
#include <list>
#include <utility>

#include "container/hash/extendible_hash_table.h"
#include "storage/page/page.h"

namespace bustub {

template <typename K, typename V>
ExtendibleHashTable<K, V>::ExtendibleHashTable(size_t bucket_size) : bucket_size_(bucket_size) {
  //初始化目录dir_，初始只有1个目录项，指向局部深度为0的桶
  dir_.push_back(std::make_shared<Bucket>(bucket_size, 0)); 
}//make_shared的作用是创建Bucket对象，返回shared_ptr指针，自动管理内存，不用手动delete

template <typename K, typename V>
//根据key计算目录索引
auto ExtendibleHashTable<K, V>::IndexOf(const K &key) -> size_t {
  // 创建一个根据全局深度定位的掩码
  //把int改成了size_t是因为哈希值本身是无符号类型，而且避免 int 位移产生符号或范围问题
  size_t mask = (size_t{1} << global_depth_) - 1;  //相当于如果global_depth_为3，则mask为0b111
  return std::hash<K>()(key) & mask;               //表示只取哈希值的低global_depth_位
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetGlobalDepth() const -> int {
  std::scoped_lock<std::mutex> lock(latch_);  // 确保线程安全
  return GetGlobalDepthInternal();            // 获取全局深度
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetGlobalDepthInternal() const -> int {
  return global_depth_;  // 内部方法：获取全局深度（不加锁）
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetLocalDepth(int dir_index) const -> int {
  std::scoped_lock<std::mutex> lock(latch_);  // 确保线程安全
  return GetLocalDepthInternal(dir_index);    // 获取指定目录索引的本地深度
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetLocalDepthInternal(int dir_index) const -> int {
  return dir_[dir_index]->GetDepth();  // 内部方法：获取本地深度（不加锁）
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetNumBuckets() const -> int {
  std::scoped_lock<std::mutex> lock(latch_);  // 确保线程安全
  return GetNumBucketsInternal();
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetNumBucketsInternal() const -> int {
  return num_buckets_;  // 内部方法：获取桶的数量（不加锁）
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::Find(const K &key, V &value) -> bool {
  std::scoped_lock<std::mutex> lock(latch_);  // 确保线程安全

  auto index = IndexOf(key);
  auto target_bucket = dir_[index];

  return target_bucket->Find(key, value);  // 查找指定键的值，没有找到就返回false
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::Remove(const K &key) -> bool {
  std::scoped_lock<std::mutex> lock(latch_);  // 确保线程安全

  auto index = IndexOf(key);
  auto target_bucket = dir_[index];

  return target_bucket->Remove(key);//return true删除成功，false key不存在
}

template <typename K, typename V>
//！！！插入键值对
void ExtendibleHashTable<K, V>::Insert(const K &key, const V &value) {
  // 加锁，保证线程安全
  std::scoped_lock<std::mutex> lock(latch_);

  auto target_bucket = dir_[IndexOf(key)];
  //先检查key是否存在于目标桶，如果键已经存在，更新其值并返回
  for (auto &item : target_bucket->GetItems()) {
    if (item.first == key) {
      item.second = value;
      return;
    }
  }

  // 检查目标桶是否满，如果满则进行分裂处理
  while (dir_[IndexOf(key)]->IsFull()) {
    size_t index = IndexOf(key);
    auto target_bucket = dir_[index];

    // 如果目标桶的深度等于全局深度，就代表目录所占的桶数满了，要增加全局深度并扩展目录
    if (target_bucket->GetDepth() == GetGlobalDepthInternal()) {
      global_depth_++;
      int capacity = dir_.size();
      dir_.resize(capacity << 1);//目录扩展为原来的两倍
      //将原有的目录项复制到新扩展的目录中
      for (int i = 0; i < capacity; i++) {
        dir_[i + capacity] = dir_[i];
      }
    }

    //掩码作用是取出哈希值第 depth_ 位，为了区分两个新桶的索引。之前是只看第 depth_-1,...,0 位
    int mask = 1 << target_bucket->GetDepth();
    auto bucket_0 = std::make_shared<Bucket>(bucket_size_, target_bucket->GetDepth() + 1);
    auto bucket_1 = std::make_shared<Bucket>(bucket_size_, target_bucket->GetDepth() + 1);

    // 将目标桶的元素重新分配到两个新桶中
    for (const auto &item : target_bucket->GetItems()) {
      size_t hash_key = std::hash<K>()(item.first);
      //0U代表无符号整数0
      if ((hash_key & mask) != 0U) {
        bucket_1->Insert(item.first, item.second);
      } else {
        bucket_0->Insert(item.first, item.second);
      }
      //相当于只看第 depth_ 位，决定放入哪个桶
    }

    num_buckets_++;

    // 更新目录，使其指向新桶
    for (size_t i = 0; i < dir_.size(); i++) {
      if (dir_[i] == target_bucket) {
        dir_[i] = ((i & mask) != 0U) ? bucket_1 : bucket_0;
      }
    }
  }

  // 插入或更新键值对
  target_bucket = dir_[IndexOf(key)];
  target_bucket->Insert(key, value);
}

//===--------------------------------------------------------------------===//
// Bucket
//===--------------------------------------------------------------------===//

template <typename K, typename V>
ExtendibleHashTable<K, V>::Bucket::Bucket(size_t array_size, int depth) : size_(array_size), depth_(depth) {}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::Bucket::Find(const K &key, V &value) -> bool {
  // 使用 std::any_of 在 list_ 中查找键
  return std::any_of(list_.begin(), list_.end(), [&key, &value](const auto &item) {
    if (item.first == key) {
      value = item.second;
      return true;
    }
    return false;
  });
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::Bucket::Remove(const K &key) -> bool {
  auto it = std::find_if(list_.begin(), list_.end(), [&key](const auto &item) { return item.first == key; });

  if (it != list_.end()) {
    list_.erase(it);  // 使用 erase 移除找到的元素
    return true;
  }

  return false;
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::Bucket::Insert(const K &key, const V &value) -> bool {
  //一定先检查key是否存在，如果key已存在，直接更新value
  for (auto &item : list_) {
    if (item.first == key) {
      item.second = value;
      return true;
    }
  }

  if (IsFull()) {
    return false;  // 如果桶已满，返回 false
  }
  list_.emplace_back(key, value);  // 否则添加键值对
  return true;
}

template class ExtendibleHashTable<page_id_t, Page *>;
template class ExtendibleHashTable<Page *, std::list<Page *>::iterator>;
template class ExtendibleHashTable<int, int>;

template class ExtendibleHashTable<int, std::string>;
template class ExtendibleHashTable<int, std::list<int>::iterator>;

}  // namespace bustub
