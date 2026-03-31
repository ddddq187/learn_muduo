#include <errno.h>
#include <fcntl.h>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <sys/eventfd.h>
#include <unistd.h>
#include <vector>

#include "Channel.h"
#include "CurrentThread.h"
#include "EventLoop.h"
#include "Logger.h"
#include "Poller.h"

__thread_local EventLoop *t_loopInThisThread = std::nullptr;
// avoid one thread create many EL
//

const int kPollerTimes = 10000; // 10s poller IO multiplexing timeout

// use eventfd to transmit data between threads,need not lock
//  #include <sys/eventfd.h>
//  int eventfd(unsigned int initval, int flags);
//  initval,初始化计数器的值
//   flags, EFD_NONBLOCK,设置socket为非阻塞。
//            EFD_CLOEXEC，执行fork的时候，在父进程中的描述符会自动关闭，子进程中的描述符保留。
//            将 eventfd 的 fd 设置为非阻塞模式：
//- 读：若 count=0，不会阻塞，直接返回 -1 并置 errno=EAGAIN；
//- 写：若 count 溢出（超过 UINT64_MAX-1），不会阻塞，直接返回错误
// ecentfd 关联一个计数器count,write使得count+1,read-1
// count>0代表可读
// 相比条件变量，fd无锁，当然也可以被原子flag或者std::latch替代

// create wakeupfd to notify subeventloop
int createEventdf() {
  int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (evtfd < 0) {
    LOG_FATAL("eventfd error %d\n", errno);
  }
  return evtfd;
}
// this 是当前 EventLoop 对象的指针，传入后是为了建立 Poller/Channel → EventLoop
// 的归属关系
EventLoop::EventLoop()
    : looping_(false), quit_(false), threadId_(CurrentThread::tid()),
      pollReturnTime_(Timestamp::now()), wakeupFd_(createEventdf()),
      callingPendingFunctors_(false), poller_(Poller::newDefaultPoller(this)),
      wakeupChannel_(new Channel(this, wakeupFd_)) {
  LOG_DEBUG("EventLoop created %p in thread %d\n", this, threadId_);
  if (t_loopInThisThread) {
    LOG_FATAL("Another EventLoop %p exists in this thread %d\n",
              t_loopInThisThread, threadId_);
  } else {
    t_loopInThisThread = this;
  }
  wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead(), this));
  // 类的非静态成员函数第一个 参数默认是this指针
  // 关于bind函数是为了到时候让对象调用this-》handleRead
  wakeupChannel_
      ->enableReading(); // 每一个EventLoop都将监听wakeupChannel_的EPOLL读事件了
}
EventLoop::~EventLoop() {
  wakeupChannel_->disableAll();
  wakeupChannel_->remove();
  ::close(wakeupFd_);
  t_loopInThisThread = nullptr;
}

// wait-exe-cb
void EventLoop::loop() {
  looping_ = true;
  quit_ = false;
  LOG_INFO("EventLoop %p start looping\n", this);

  while (!quit_) {
    activeChannels_.clear();

    pollRetureTime_ = poller_->poll(kPollerTimeMs, &activeChannels_);
    for (Channel *channel : activeChannels_) {
      // poller监听哪些channel发生事件，然后上传到EventLoop,通知channel 处理事件
      channel->handleEvent(pollRetureTime_);
    }
    // 执行回调，mainloop（mainreactor）主要：
    // accept接受连接-将accept返回的fd打包为channel -
    // TcpServer：：newConnection通过轮询把conect对象分配给subloop
    // maintop调用queueInloop将回调加入subloop
    // 该回调需要在subloop执行，但是subloop还在poller_->poll处阻塞，所以queueInLoop通过wakeup唤醒subloop
    doPendingFunctors();
  }
  LOG_INFO("EventLoop %p stop looping.\n", this);
  looping_ = false;
}
/**
 * 退出事件循环
 * 1. 如果loop在自己的线程中调用quit成功了
 * 说明当前线程已经执行完毕了loop()函数的poller_->poll并退出
 * 2. 如果不是当前EventLoop所属线程中调用quit退出EventLoop
 * 需要唤醒EventLoop所属线程的epoll_wait
 *
 * 比如在一个subloop(worker)中调用mainloop(IO)的quit时
 * 需要唤醒mainloop(IO)的poller_->poll 让其执行完loop()函数
 *
 * ！！！ 注意： 正常情况下 mainloop负责请求连接 将回调写入subloop中
 * 通过生产者消费者模型即可实现线程安全的队列 ！！！  但是muduo通过wakeup()机制
 * 使用eventfd创建的wakeupFd_ notify 使得mainloop和subloop之间能够进行通信
 **/
void EventLoop::quit() {
  quit_ = true;
  if (!isInLoopThread()) {
    wakeup();
    // poller—>poll函数底层是epoll_wait，没有事件一直阻塞，超时时间是10s
    // 本线程调用quit,由于此时loop在运行，没有阻塞，修改flag后下次循环直接退出
    // 其他线程调用quit,比如主线程想关闭子线程，主线程flag写成true但是子线程不知道，要等10s超市才退出
    // 这时候调用wakeup（）像wakeupFd_写入一个字节，触发epoll读事件，epoll_wait立即返回解除阻塞
    // 线程回到循环，判断！quit然后退出loop
  }
}
// 当前loop中执行callback
void EventLoop::runInLoop(Functor cb) {
  if (isInLoopThread()) {
    cb();
  } else {
    queueInLoop();
    // 在非当前EventLoop线程中执行cb，就需要唤醒EventLoop所在线程执行cb
  }
}

void EventLoop::queueInLoop(Functor cb) {
  {
    std::unique_lock<std::mutex> lock(mutex_);
    pendingFunctors_.emplace_back(
        cb); // 跨线程共享队列，加锁。
             // 使用{}
             // 局部作用域，最小化临界区（锁的持有时间）
    /**
     * || callingPendingFunctors的意思是 当前loop正在执行回调中
     * 但是loop的pendingFunctors_中又加入了新的回调 需要通过wakeup写事件
     * 唤醒相应的需要执行上面回调操作的loop的线程
     * 让loop()下一次poller_->poll()不再阻塞（阻塞的话会延迟前一次新加入的回调的执行），然后
     * 继续执行pendingFunctors_中的回调函数
     **/
  }
  if (!isInLoopThread() || callingPendingFunctors_) {
    wakeup();
  }
}

//
//
void EventLoop::handleRead() {
  uint64_t one = 1;
  ssize_t n = read(wakeupFd_, &one, sizeof(one));
  if (n != sizeof(one)) {
    LOG_ERROR("EventLoop::handleRead() reads %lu bytes instead of 8\n", n);
  }
}

// 用来唤醒loop所在线程 向wakeupFd_写一个数据 wakeupChannel就发生读事件
// 当前loop线程就会被唤醒
void EventLoop::wakeup() {
  uint64_t one = 1;
  ssize_t n = write(wakeupFd_, &one, sizeof(one));
  if (n != sizeof(one)) {
    LOG_ERROR("EventLoop::wakeup() writes %lu bytes instead of 8\n", n);
  }
}
/*
其他线程调用 wakeup()
  ↓
write(wakeupFd_, 1) → 内核计数器+1
  ↓
epoll_wait 检测到读事件 → 解除阻塞
  ↓
自动调用 handleRead()
  ↓
read(wakeupFd_) → 内核计数器清零
  ↓
事件处理完毕，不会重复触发
*/
// poller方法
void EventLoop::updateChannel(Channel *channel) {
  poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel *channel) {
  poller_->removeChannel(channel);
}
// 把 pendingFunctors_ 队列里的所有任务拿出来串行执行
void EventLoop::doPendingFunctors() {
  std::vector<Functor> functors;
  callingPendingFunctors_ =
      true; // 当前EventLoop在执行回调函数，和queueInLoop呼应
            // 如果有新的回调则立即wakeup
  {
    std::unique_lock<std::mutex> lock(mutex_);
    functors.swap(pendingFunctors_);
    // swap 是交换两个 vector 的内部内存地址，O (1) 极速操作，不是拷贝！
    // 局部变量 functors：拿到了所有待执行的回调
    // 共享队列 pendingFunctors_：变成空队列
    //  交换的方式减少了锁的临界区范围 提升效率 同时避免了死锁
    //  如果执行functor()在临界区内 且functor()中调用queueInLoop()就会产生死锁
  }
  for (const Functor &functor : functors) {
    functor();
  }
  callingPendingFunctors_ = false;
}
