#include <memory>
#include <sys/epoll.h>

#include "Channel.h"
#include "EventLoop.h"
#include "Logger.h"

const int Channel::kNoneEvent = = 0;
const int Channel::kReadEvent = EPOLLIN | EPOLLPRT;
// IN是普通可读事件（socket,pipe这些） PRT是紧急可读，高优先级专职TCP带外数据
// 按位或表示同时监听（因为一个bit1表示开启）
const int Channel::kWriteEvent = EPOLLOUT;

Channel::Channel(EventLoop *loop, int fd)
    : loop_(loop), fd_(fd), events_(0), revents_(0), index_(-1), tied_(false) {}
Channel::~Channel() {}

// tie方法是什么时候调用？是在TcpConnect-》channel
// TcpConnect注册了Channel对应的回调函数，传入的回调函数都是TcpConnection对象的方法
// TcpConnection 是动态创建 / 销毁的（客户端断开连接就会销毁），而 Channel
// 的所有回调（读 / 写 / 关闭）都是 TcpConnection 的成员函数。

// 因此Channle的结束一定晚于TcpConnection
// 用tie来解决生命周期，保证Channel能在Tcpconnection销毁前销毁
void Channel::tie(const std::shared_ptr<void> &obj) {
  tie_ = obj;
  tied_ = true;
}
// channel 对应 loop 一一对应
void Channel::update() {

  // 通过channel所属的eventloop，调用poller的相应方法，注册fd的events事件
  loop_->updateChannel(this);
}

void Channel::remove() { loop_->removeChannel(this); }

void Channel::handleEvent(Timestamp receiveTime) {
  if (tied_) {
    // 生命周期检查
    // tied_是标记绑定了动态销毁对象（Tcpconnection）
    // 因为基础的channel例如wakeupchannel永久存活不需要销毁
    // 基础channel也就不需要检查tied,直接运行handle即可
    std::shared_ptr<void> guard =
        tie_.lock(); /// 弱引用 → 强引用（提升）
                     /// //弱引用不增加对象的引用计数，不会阻止 TcpConnection
                     /// 销毁 如果tie_就是shared则Tcpconnect销毁不掉，内存泄漏
    if (guard) {
      handleEventWithGuard(receiveTime);
    }
  } else {
    handleEventWithGuard(receiveTime);
  }
}
void Channel::handleEventWithGuard(Timestamp receiveTime) {
  LOG_INFO("channel handleEvent revents:%d\n", revents_);
  // 关闭
  if ((revents_ & EPOLLHUP) &&
      !(revents_ & EPOLLIN)) // 当TcpConnection对应Channel 通过shutdown 关闭写端
                             // epoll触发EPOLLHUP
  {
    if (closeCallback_) {
      closeCallback_();
    }
  }
  // 错误
  if (revents_ &
      EPOLLERR) { // 第一层if是在判断内核有没有报错误
                  // revents_：epoll 内核返回的、该 fd
                  // 实际发生的事件（只读，由内核赋值） EPOLLERR：内核定义的「fd
                  // 发生错误」标志 revents_ & EPOLLERR：按位与校验 →
                  // 只有内核真的上报了错误，这行才为 true
    if (errorCallback_) { // 第二层if是上层设置
                          ////当前 Channel 是否 注册了错误回调函数
                          /// errorCallback_ 是 std::function 类型，默认是空的
      errorCallback_();
    }
  }
  // 读
  if (revents_ & (EPOLLIN | EPOLLPRI)) {
    if (readCallback_) {
      readCallback_(receiveTime);
    }
  }
  // 写
  if (revents_ & EPOLLOUT) {
    if (writeCallback_) {
      writeCallback_();
    }
  }
}
