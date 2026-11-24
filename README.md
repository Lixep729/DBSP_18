# Project 1

## 作业概述

本仓库完成了 CMU BusTub 的 Project 1。`P1_src/` 是老师提供的初始作业代码，提供了需要实现的头文件接口、成员变量和测试框架；本组是在这份初始代码基础上完成三部分功能的修改和实现，并使用项目提供的测试程序进行验证。

Project 1 的主要目标是实现数据库缓冲池相关的关键数据结构：

- 使用可扩展哈希表维护页面标识符到缓冲池帧编号的映射。
- 使用 LRU-K Replacer 对可淘汰页面帧进行淘汰决策。
- 使用 Buffer Pool Manager Instance 协调页面获取、释放、刷盘和淘汰过程。

## 分工

- 李先铃负责 Extendible Hash Table（可扩展哈希表）和 LRU-K Replacer（LRU-K 页替换算法）。
- 郑杰负责 Buffer Pool Manager Instance（缓冲池管理器实例）。

三部分共同构成完整的缓冲池页面管理流程：缓冲池接收页面请求，哈希表查找或记录“页面号到帧编号”的映射，LRU-K 根据页面访问历史决定淘汰顺序。

## 我们完成的工作

### 1. Extendible Hash Table

在老师提供的 `extendible_hash_table.h` 接口基础上，实现了 `extendible_hash_table.cpp` 中的核心功能：

- 实现根据哈希值查找键值。
- 实现插入键值；重复插入已有键时更新对应值，不重复创建无意义的新桶。
- 实现删除键值，并保持剩余目录项结构正确。
- 实现全局深度和局部深度的查询。
- 桶容量达到上限且局部深度小于全局深度时，对桶进行分裂，并重新分配桶中的键值。
- 桶的局部深度等于全局深度时，先将目录容量扩展两倍，再进行桶分裂。
- 分裂后更新目录中指向新旧桶的索引，使相同哈希前缀继续共享桶，不同哈希前缀指向对应桶。
- 使用互斥锁保护目录、桶和深度等共享数据结构，保证并发访问安全。

当前实现未强制要求桶合并或目录收缩；删除操作完成后保留现有目录结构，不影响查找、插入和后续分裂。

### 2. LRU-K Replacer

在老师提供的接口基础上，修改了 `lru_k_replacer.h` 和 `lru_k_replacer.cpp`：

- 为每个帧记录最近最多 K 次访问的时间戳。
- 区分可淘汰帧和不可淘汰帧；不可淘汰帧不会被淘汰。
- 实现页面或帧被 PIN 时的不可淘汰状态设置，以及 UNPIN 后的可淘汰状态设置。
- 实现淘汰候选帧的选择：
  - 访问次数不足 K 次的帧具有无穷大的 backward K-distance，优先淘汰。
  - 多个无穷距离候选帧按照最早访问时间选择。
  - 达到 K 次访问的帧比较第 K 近访问时间，第 K 近访问时间更早的帧更优先淘汰。
- 实现淘汰成功后的帧信息清理，以及可淘汰帧数量的维护。
- 实现移除指定帧并清除其访问历史。
- 使用互斥锁保护访问历史、淘汰状态和帧集合等共享数据，支持并发调用。

### 3. Buffer Pool Manager Instance

在老师提供的接口基础上，完成了 `buffer_pool_manager_instance.cpp` 的主要功能：

- 接收页面号和页标识符，按需查找、加载或淘汰页面。
- 维护 Page ID 到 Frame ID 的映射，并与可扩展哈希表配合工作。
- 实现页面获取和释放，包括 PIN 和 UNPIN 状态更新。
- 实现脏页标记与刷盘逻辑。
- 实现指定页面的刷新到磁盘。
- 实现所有脏页刷新到磁盘。
- 实现新页面创建以及无空闲帧时的页面淘汰。
- 实现指定页面删除，并同步更新页表和替换器状态。
- 在需要淘汰时调用 LRU-K Replacer，并保证帧未被引用时才允许淘汰。

## 并发与线程安全

本项目涉及多个线程同时访问可扩展哈希表、LRU-K 替换器和缓冲池内部状态，因此共享数据结构都受互斥锁保护。实现中使用 C++17 的标准互斥锁或 `std::scoped_lock`，避免多个线程同时修改桶、目录、帧状态、访问历史和页面映射等数据。

## 测试与验证

项目使用官方测试和并发测试验证实现，已通过：

- LRU-K Replacer：4/4 项测试通过。
- Extendible Hash Table：12/12 项测试通过。
- Buffer Pool Manager Instance：13/13 项测试通过。
- 格式检查和代码修改差异检查通过。

测试入口脚本为 `Project1/p1_test.sh`，主要执行：

- 格式化检查：`make format`
- Lint 检查：`make check-lint`
- P1 静态检查：`make check-clang-tidy-p1`
- 可扩展哈希表测试：`extendible_hash_table_test`
- LRU-K 替换器测试：`lru_k_replacer_test`
- 缓冲池管理器测试：`buffer_pool_manager_instance_test`

三组测试截图保存在 `Project1/screenshots/`：

- `lru_k_replacer_test.png`
- `extendible_hash_table_test.png`
- `buffer_pool_manager_instance_test.png`

## 提交说明

`Project1/` 是完整的开发和测试工程，包含源码、测试、构建配置、第三方依赖、测试脚本和截图。

`project1-submission.zip` 是最终提交包，只包含实验要求的 6 个代码文件，包含测试截图。

