//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// buffer_pool_manager_instance.cpp
//
// Identification: src/buffer/buffer_pool_manager.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/buffer_pool_manager_instance.h"

#include "common/exception.h"
#include "common/macros.h"

namespace bustub {

BufferPoolManagerInstance::BufferPoolManagerInstance(size_t pool_size, DiskManager *disk_manager, size_t replacer_k,
                                                     LogManager *log_manager)
    : pool_size_(pool_size), disk_manager_(disk_manager), log_manager_(log_manager) {
  // we allocate a consecutive memory space for the buffer pool
  pages_ = new Page[pool_size_];
  page_table_ = new ExtendibleHashTable<page_id_t, frame_id_t>(bucket_size_);
  replacer_ = new LRUKReplacer(pool_size, replacer_k);

  // Initially, every page is in the free list.
  for (size_t i = 0; i < pool_size_; ++i) {
    free_list_.emplace_back(static_cast<int>(i));
  }
}

BufferPoolManagerInstance::~BufferPoolManagerInstance() {
  delete[] pages_;
  delete page_table_;
  delete replacer_;
}

auto BufferPoolManagerInstance::NewPgImp(page_id_t *page_id) -> Page * {
  std::scoped_lock<std::mutex> lock(latch_);  // 通过互斥锁确保多线程安全

  // 获取空闲页
  frame_id_t frame_id;
  // 检查空闲列表是否为空
  if (!free_list_.empty()) {
    frame_id = free_list_.front();
    free_list_.pop_front();
  }
  // 如果空闲列表为空，则尝试逐出一个页面
  else {
    if (!replacer_->Evict(&frame_id)) {
      // 若逐出失败，返回nullptr
      return nullptr;
    }
    // 逐出成功，获取磁盘号，操作页面
    page_id_t evicted_page_id = pages_[frame_id].GetPageId();

    // 如果被逐出的页面是脏的，则将其写回磁盘
    if (pages_[frame_id].IsDirty()) {
      disk_manager_->WritePage(evicted_page_id, pages_[frame_id].GetData());
      pages_[frame_id].is_dirty_ = false;
    }

    // 重置被逐出的页面内存
    pages_[frame_id].ResetMemory();

    // 从页表中移除被逐出的页面(Remove)
    page_table_->Remove(evicted_page_id);
  }
  // 成功获取，从磁盘分配新页面
  *page_id = AllocatePage();
  // 在页表中插入新页面(Insert)
  page_table_->Insert(*page_id, frame_id);

  // 初始化新页面的元数据
  pages_[frame_id].page_id_ = *page_id;
  pages_[frame_id].pin_count_ = 1;
  pages_[frame_id].is_dirty_ = false;

  // 记录页面访问和设置页面为不可逐出
  replacer_->RecordAccess(frame_id);
  replacer_->SetEvictable(frame_id, false);

  return &pages_[frame_id];
}

auto BufferPoolManagerInstance::FetchPgImp(page_id_t page_id) -> Page * {
  // 通过互斥锁确保多线程安全
  std::scoped_lock<std::mutex> lock(latch_);

  frame_id_t frame_id;

  // 尝试从页表中查找页面(Find)
  if (page_table_->Find(page_id, frame_id)) {
    // 如果找到了页面，增加其引用计数并返回该页面
    pages_[frame_id].pin_count_++;
    replacer_->RecordAccess(frame_id);
    replacer_->SetEvictable(frame_id, false);
    return &pages_[frame_id];
  }

  // 未找到页面，需要读到缓冲区
  //  从空闲列表或通过逐出操作获取一个空闲帧
  if (!free_list_.empty()) {
    frame_id = free_list_.front();
    free_list_.pop_front();
  } else {
    if (!replacer_->Evict(&frame_id)) {
      // 如果无法逐出页面，则返回空指针
      return nullptr;
    }
    // 处理被逐出的页面
    auto evicted_page_id = pages_[frame_id].GetPageId();
    if (pages_[frame_id].IsDirty()) {
      // 如果页面是脏的，则写回磁盘
      disk_manager_->WritePage(evicted_page_id, pages_[frame_id].GetData());
      pages_[frame_id].is_dirty_ = false;
    }
    // 重置页面内容，移除映射(Remove)
    pages_[frame_id].ResetMemory();
    page_table_->Remove(evicted_page_id);
  }

  // 将新页面加载到帧中
  disk_manager_->ReadPage(page_id, pages_[frame_id].GetData());
  // 插入page_table(Insert)
  page_table_->Insert(page_id, frame_id);
  pages_[frame_id].page_id_ = page_id;
  pages_[frame_id].pin_count_ = 1;
  pages_[frame_id].is_dirty_ = false;
  replacer_->RecordAccess(frame_id);
  replacer_->SetEvictable(frame_id, false);

  return &pages_[frame_id];
}

auto BufferPoolManagerInstance::UnpinPgImp(page_id_t page_id, bool is_dirty) -> bool {
  // 设置互斥锁保证多线程安全
  std::scoped_lock<std::mutex> lock(latch_);
  // 尝试在内存中找到该页面(Find)
  frame_id_t frame_id;
  if (!page_table_->Find(page_id, frame_id)) {
    // 未找到返回false
    return false;
  } else {
    if (pages_[frame_id].pin_count_ == 0) {
      return false;
    } else {
      // 找到页面后引用计数--，若为零则设为可驱逐

      if (--pages_[frame_id].pin_count_ == 0) {
        replacer_->SetEvictable(frame_id, true);
      }
      // 设置脏位
      pages_[frame_id].is_dirty_ = pages_[frame_id].is_dirty_ | is_dirty;
      return true;
    }
  }
  return false;
}

auto BufferPoolManagerInstance::FlushPgImp(page_id_t page_id) -> bool {
  // 设置互斥锁保证多线程安全
  std::scoped_lock<std::mutex> lock(latch_);
  // 尝试从内存中找到该页面(Find)
  frame_id_t frame_id;
  if (!page_table_->Find(page_id, frame_id)) {
    // 未找到返回false
    return false;
  }
  // 强制写回磁盘，设置脏位为0
  disk_manager_->WritePage(page_id, pages_[frame_id].GetData());
  pages_[frame_id].is_dirty_ = false;
  return true;
}

void BufferPoolManagerInstance::FlushAllPgsImp() {
  // 设置互斥锁保证多线程安全
  std::scoped_lock<std::mutex> lock(latch_);
  // 遍历pages_进行刷盘
  for (size_t i = 0; i < pool_size_; i++) {
    // 空闲列表不处理，只考虑有对应磁盘页面的帧
    if (pages_[i].page_id_ != INVALID_PAGE_ID) {
      disk_manager_->WritePage(pages_[i].GetPageId(), pages_[i].GetData());
      pages_[i].is_dirty_ = false;
    }
  }
}

auto BufferPoolManagerInstance::DeletePgImp(page_id_t page_id) -> bool {
  // 设置互斥锁保证多线程安全
  std::scoped_lock<std::mutex> lock(latch_);
  // 尝试找到该页面(Find)
  frame_id_t frame_id;
  if (!page_table_->Find(page_id, frame_id)) {
    // 未找到返回true
    return true;
  }
  if (pages_[frame_id].pin_count_ > 0) {
    // 被引用不能删除，返回false
    return false;
  }
  // 从page_table_中移除映射(Remove)
  page_table_->Remove(page_id);
  // 从replacer_移除记录
  replacer_->Remove(frame_id);

  pages_[frame_id].ResetMemory();
  free_list_.emplace_back(frame_id);
  // 磁盘释放该页
  DeallocatePage(page_id);
  return true;
}

auto BufferPoolManagerInstance::AllocatePage() -> page_id_t { return next_page_id_++; }

}  // namespace bustub
