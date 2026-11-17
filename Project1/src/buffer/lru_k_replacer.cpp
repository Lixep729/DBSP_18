//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// lru_k_replacer.cpp
//
// Identification: src/buffer/lru_k_replacer.cpp
//
// Copyright (c) 2015-2022, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/lru_k_replacer.h"

#include <stdexcept>

namespace bustub {

LRUKReplacer::LRUKReplacer(size_t num_frames, size_t k) : replacer_size_(num_frames), k_(k) {
  if (k_ == 0) {
    throw std::invalid_argument("LRU-K requires k to be greater than zero");
  }
}

/**
 * 尝试逐出一个页面。
 * @param frame_id 指向将要被逐出的帧ID的指针。
 * @return 如果成功逐出页面，返回true；否则返回false。
 */
auto LRUKReplacer::Evict(frame_id_t *frame_id) -> bool {
  std::scoped_lock<std::mutex> lock(latch_);

  // 检查当前是否有可逐出的页面
  if (curr_size_ == 0) {
    return false;  // 没有可逐出的页面
  }

  //！优先逐出访问次数不足k次的页面（历史页）。若无就从满k次的（缓存区）里选，如何选择呢，是靠时间戳，距离当前时间越远的先淘汰

  auto victim = frame_info_.end();//指向的是容器最后一个元素的后面，不指向真实记录
  for (auto it = frame_info_.begin(); it != frame_info_.end(); ++it) {
    //跳过不可驱逐的帧
    if (!it->second.evictable_) {
      continue;
    }
    //如果还没有选中候选victim，先把当前帧设为victim
    if (victim == frame_info_.end()) {
      victim = it;
      continue;
    }

    const auto &candidate_history = it->second.history_;
    const auto &victim_history = victim->second.history_;
    //判断是否属于历史区
    const bool candidate_is_infinite = candidate_history.size() < k_;
    const bool victim_is_infinite = victim_history.size() < k_;

    if (candidate_is_infinite != victim_is_infinite) {
      //如果候选是历史区，victim是缓存区，则优先驱逐候选，所以把候选替换到victim
      if (candidate_is_infinite) {
        victim = it;
      }
      continue;
    }
    //如果都在历史区或都在缓存区，就比较队首（距离时间最远的）时间戳，时间戳越小（距离当前时间就更远）的优先淘汰
    if (candidate_history.front() < victim_history.front()) {
      victim = it;
    }
  }

  // 重置访问次数并从列表中移除
  *frame_id = victim->first;
  frame_info_.erase(victim);
  curr_size_--;
  return true;
}

/**
 * 记录对指定页面的访问。
 * @param frame_id 被访问的帧ID。
 */
void LRUKReplacer::RecordAccess(frame_id_t frame_id) {
  std::scoped_lock<std::mutex> lock(latch_);//加互斥锁，保证多线程并发访问安全

  //检查frame_id是否合法
  CheckFrameId(frame_id);

  //获取该frame的信息，如果不存在则创建一个新的FrameInfo
  auto &info = frame_info_[frame_id];
  info.history_.push_back(current_timestamp_);//记录当前时间戳到访问历史队列尾部
  current_timestamp_++;
  if (info.history_.size() > k_) {
    info.history_.pop_front();//如果超过k个，就从头部移除
  }
}

/**
 * 设置指定页面的逐出状态。
 * @param frame_id 要设置的帧ID。
 * @param set_evictable 是否可逐出。
 */
void LRUKReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  std::scoped_lock<std::mutex> lock(latch_);//加锁，多线程安全

  //检查frame_id的有效性
  CheckFrameId(frame_id);

  //检查是否存在这个frame
  auto it = frame_info_.find(frame_id);

  //没有找到就会返回frame_info_.end()，此时直接返回空
  if (it == frame_info_.end()) {
    return;
  }

  //如果当前的evictable状态与要设置的状态相同，也返回空
  if (it->second.evictable_ == set_evictable) {
    return;
  }
  
  //其余情况：更新状态
  it->second.evictable_ = set_evictable;
  curr_size_ += set_evictable ? 1 : -1;//如果标记为可以淘汰，就+1
}

/**
 * @brief 从LRUK替换器中移除指定帧ID的函数。
 *
 * @param frame_id 要移除的帧ID。
 *
 * @throws std::exception 如果frame_id大于替换器的大小或其他错误情况。
 */
void LRUKReplacer::Remove(frame_id_t frame_id) {
  std::scoped_lock<std::mutex> lock(latch_);
  CheckFrameId(frame_id);

  auto it = frame_info_.find(frame_id);
  if (it == frame_info_.end()) {
    // 如果访问计数为0，则不执行任何操作，直接返回
    return;
  }

  // 如果帧不可驱逐，则抛出异常
  if (!it->second.evictable_) {
    throw std::exception();
  }

  frame_info_.erase(it);
  curr_size_--;
}

auto LRUKReplacer::Size() -> size_t {
  std::scoped_lock<std::mutex> lock(latch_);
  return curr_size_;
}

void LRUKReplacer::CheckFrameId(frame_id_t frame_id) const {
  if (frame_id < 0 || static_cast<size_t>(frame_id) >= replacer_size_) {
    throw std::out_of_range("frame id is out of range");
  }
}

}  // namespace bustub
