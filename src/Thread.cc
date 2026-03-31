#include <atomic>
#include <cstdio>
#include <memory>
#include <semaphore.h>
#include <string>
#include <thread>
#include <utility>

#include "CurrentThread.h"
#include "Thread.h"

std::atomic_int numCreated_(0);
// 类的静态变量必须在cc中才能定义，h中只能声明（不分配内存）
// 否则会多重定义
// cpp17引入 inline static可以直接头文件内定义

Thread::Thread(ThreadFunc func, const std::string &name)
    : started_(false), joined_(false), tid_(0), func_(std::move(func)),
      name_(name) {
  setDefaultName();
}
// 私有变量有thread_，但是std::thread一经创建，线程函数就立即运行了
// 这不是我们想要的
// 此外构造函数不适合抛出异常或者阻塞，我们需要tid,所以把信号量操作也放在start让用户管理
// 构造函数：只做对象初始化（名称、计数、保存函数），不执行业务逻辑；
// start()：只做创建线程 + 启动线程；
// join()：只做等待线程结束。
Thread::~Thread() {
  if (started_ && !joined_) {
    thread_->detach();
  }
}
void Thread::start() {
  started_ = true;
  sem_t sem;
  sem_init(&sem, false, 0); // 这里主线程没有阻塞
  thread_ = std::shared_ptr<std::thread>(new std::thread([&]() {
    // lambda里的[&]：表示捕获外部变量的引用（比如sem、tid_、func_），所以新线程能访问这些变量；
    //  新线程先给tid_赋值（核心！必须先完成这个操作）
    tid_ = CurrentThread::tid();
    // 唤醒阻塞的主线程去干别的事情，因为核心就是tid
    sem_post(&sem);
    // 新线程执行业务逻辑
    func_(); // func_是函数名字，用的时候要加（）
  }));
  sem_wait(&sem); // 主线程阻塞在这里，因为计数器初始化0,这里只能阻塞
}

// semaphore信号量就是 计数器+阻塞唤醒
// 线程/进程间同步工具
// 计数器>0,线程可以拿走一个计数同时计数器减1
// 计数器=0,线程必须阻塞直到其他线程归还一个计数
// int sem_init(sem_t *sem, int pshared, unsigned int value);
// sem：要初始化的信号量，pshared：0（线程间同步），非0（进程间），value（初始计数器）
// int sem_post(sem_t *sem);计数器+1 如果有线程阻塞则唤醒其中一个
// int sem_wait(sem_t
// *sem);计数器大于0则减1继续执行，计数器等于0则阻塞线程直到被sempost唤醒
// start：创建新线程，并确保主线程（调用 start
// 的线程）必须等新线程完成tid_的赋值后，才能继续执行
// sem 的唯一作用就是同步主线程和新线程，确保
// tid_赋值完成后主线程才继续，没有其他复杂逻辑。
// 严格来说，用完信号量应该调用sem_destroy(&sem)释放资源，但这里sem是start()的局部变量，函数执行完后会自动销毁，muduo
// 这里做了简化； 保证主线程必须等子线程赋值 tid 后才能继续： 情况
// 1：主线程先执行到 sem_wait
// 1. 主线程创建子线程 →
// 2. 主线程执行 sem_wait（因 sem 计数 = 0，阻塞） →
// 3. 子线程被调度，执行 tid 赋值 →
// 4. 子线程 sem_post（计数 + 1） →
// 5. 主线程被唤醒，继续执行
// 主线程阻塞期间，子线程完成了 tid 赋值
// 情况 2：子线程先被调度
// 1. 主线程创建子线程 →
// 2. 子线程先被调度，执行 tid 赋值 →
// 3. 子线程 sem_post（计数从 0→1） →
// 4. 主线程执行 sem_wait（因计数 = 1，直接减 1 通过，不阻塞）
// 主线程执行 sem_wait 前，子线程已完成 tid 赋值，无需阻塞也能保证 tid 有效
// tid_是子线程的pid,主线程的 PID（也是主线程
// tid）由进程管理，无需Thread类存储（Thread类的职责是封装 “工作子线程”）。
void Thread::join() {
  joined_ = true;
  thread_->join();
}

void Thread::setDefaultName() {
  int num = ++numCreated_;
  if (name_.empty()) {
    char buf[32] = {0};
    snprintf(buf, sizeof buf, "Thread%d", num);
    // int snprintf(char *str, size_t size, const char *format, ...);
    // 将格式化后的字符串写入指定缓冲区，并限制写入长度，避免溢出
    // str：目标缓冲区（如代码中的buf）；
    // size：缓冲区的总字节数（如代码中的sizeof buf）；
    // format：格式化模板（如代码中的"Thread%d"）；
    // sizeof buf = sizeof(buf) = 32
    name_ = buf;
  }
}
// 值得注意的是，现在cpp不需要信号量，有更高级的api
// mutex+conditional_variable尝试改写
// cpp20引入了std::counting_semaphore作为直接替代
// 事实上为了体现主线程等待子线程完成一次性操作这种情况
// 直接使用std::latch
