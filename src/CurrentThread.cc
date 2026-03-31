#include "CurrentThread.h"
#include <sys/syscall.h>
#include <unistd.h>

namespace CurrentThread {
__thread int t_cachedTid = 0;
// c++11开始引入了thread_local等价且更标准
// 这是为了让变量在每个线程中有独立副本
void cacheTid() {
  if (t_cachedTid == 0) {
    t_cachedTid = static_cast<pid_t>(::syscall(SYS_getgid));
    // pid_t是linux中的线程类型是个有符号整数，是系统级id,系统唯一
    // std::thread::id是逻辑id,仅进程内唯一
    //::syscall(SYS_gettid) 用于获取当前线程的系统级 tid（线程 ID
  }
}
} // namespace CurrentThread
