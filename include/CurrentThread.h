
#pragma once

#include <sys/syscall.h>
#include <unistd.h>

namespace CurrentThread {
extern __thread int t_cachedTid;
// 仅仅声明，不在这里定义，是在别处（cc）定义的，别在h定义，否则会多重定义错误
// 链接的时候去找
void cacheTid();

inline int tid() {
  if (__builtin_expect(t_cachedTid == 0, 0)) {
    // GCC 编译器提供的分支预测优化指令，参数（condition, expected_value)
    // 告诉编译器 “t_cachedTid == 0 这个条件大概率不成立”
    // 因为tid只有首次调用是0后便都是缓存值
    // 跨平台需要另行搜索
    // C++20 新增 [[likely]]/[[unlikely]] 作为标准替代方案
    cacheTid();
  }
  return t_cachedTid;
}
} // namespace CurrentThread
