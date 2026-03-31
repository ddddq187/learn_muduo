#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "CurrentThread.h"
#include "Timestamp.h"
#include "noncopyable.h"

class Channel;
class Poller;

class EventLoop : noncopyable {
public:
  using Functor = std::function<void()>;

  EventLoop();
  ~EventLoop();

  // start EventLoop
  void loop();

  // quit EventLoop

  void quit();

  Timestamp pollReturnTime() { return pollRetureTime_; };

  // 执行/唤醒
  void runInLoop(Functor cb);

  // 入队
  void queueInLoop(Functor cb);

  void wakeup();

  // Poller
  void updateChannel(Channel *channel);
  void removeChannel(Channel *channle);
  bool hasChannel(Channel *channel);

  // whether this EL within its own thread
  bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); }

private:
  // eventd return wakeupFd_,
  // when wakeup,read 8bytes of wakeupFd_,and wakeup the blocked epoll
  void handleRead();

  // exe the upper-cb
  void doPendingFunctors();

  using ChannelList = std::vector<Channel *>;

  std::atomic_bool looping_;
  std::atomic_bool quit_;

  const pid_t threadId_;

  Timestamp pollRetureTime_;

  std::unique_ptr<Poller> poller_;

  int wakeupFd_; // maiploop get a Channle,then choose a subloop,use wakeupFd_
                 // wakeup subloop process Channel
  std::unique_ptr<Channel> wakeupChannel_;

  ChannelList activeChannels_; // returns all channels with events detected by
                               // the poller

  std::atomic_bool callingPendingFunctors_; // whether current loop need exe cb

  std::vector<Functor> pendingFunctors_; // all cb which loop need to process

  std::mutex mutex_;
};
