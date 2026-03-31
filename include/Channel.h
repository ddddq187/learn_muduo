#pragma once

#include <algorithm>
#include <functional>
#include <memory>

#include "Timestamp.h"
#include "noncopyable.h"

class EventLoop;

class Channel : noncopyable {

public:
  using EventCallback = std::function<void()>;
  using ReadEventCallback = std::function<void(Timestamp)>;

  Channel(EventLoop *loop, int fd);
  ~Channel();

  void handleEvent(
      Timestamp
          receiveTime); // fd得到poller通知，处理事件handleEvent在EvetLoop::loop中调用

  void setReadCallback(ReadEventCallback cb) { readCallback_ = std::move(cb); };
  void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); };
  void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); };
  void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); };

  // tie防止channel被手动remove掉的时候 channel还在执行回调
  void tie(const std::unique_ptr<void> &);
  // 设置fd相应的事件状态 相当于epoll_ctl add delete
  void enableReading() {
    events_ |= kReadEvent;
    update();
  }
  void disableReading() {
    events_ &= ~kReadEvent;
    update();
  }
  void enableWriting() {
    events_ |= kWriteEvent;
    update();
  }
  void disableWriting() {
    events_ &= ~kWriteEvent;
    update();
  }
  void disableAll() {
    events_ = kNoneEvent;
    update();
  }
  //  |= → 打开指定开关
  // 不管其他开关是开是关，只把读开关打开
  //&= ~ → 关闭指定开关
  // 不管其他开关是开是关，只把读开关关闭}

  // 返回fd当前的事件状态
  bool isNoneEvent() const { return events_ == kNoneEvent; }
  bool isWriting() const { return events_ & kWriteEvent; }
  bool isReading() const { return events_ & kReadEvent; }

  int index() { return index_; }
  void set_index(int idx) { index_ = idx; }

  // one loop per thread
  EventLoop *ownerLoop() { return loop_; }
  void remove();

private:
  void update();
  void handleEventWithGuard(Timestamp receiveTime);

  static const int kNoneEvent;
  static const int kReadEvent;
  static const int kWriteEvent;

  EventLoop *loop_; // 事件循环
  const int fd_;    // fd，Poller监听的对象
  int events_;      // 注册fd感兴趣的事件
  int revents_;     // Poller返回的具体发生的事件
  int index_;

  std::weak_ptr<void> tie_;
  bool tied_;

  // 因为channel通道里可获知fd最终发生的具体的事件events，所以它负责调用具体事件的回调操作
  ReadEventCallback readCallback_;
  EventCallback writeCallback_;
  EventCallback closeCallback_;
  EventCallback errorCallback_;
};
