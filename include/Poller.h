#pragma once

#include <unordered_map>
#include <vector>

#include "Timestamp.h"
#include "noncopyable.h"

class Channel;
class EventLoop;

class Poller {
  piblic : using ChannelList = std::vector<Channel *>;
  Poller(EventLoop *loop);
  virtual ~Poller() = default;

  virtual Timestamp poll(int timeoutMs, ChannelList *activeChannels) = 0;
  virtual void updateChannel(Channel *channel) = 0;
  virtual void removeChannel(Channel *channel) = 0;

  // channel是否在Poller
  bool hasChannel(Channel *channel) const;
  // 给EvevtLoop的接口，让EventLoop知道默认IO复用的具体实现
  static Poller *newDefaultPoller(EventLoop *loop);

protected:
  // key：sockfd value：fd对应的channel
  using ChannelMap = std::unordered_map<int, Channel *>;
  ChannelMap channels_;

private:
  EventLoop *ownerLoop_; // poller所属事件循环EventLoop
}
