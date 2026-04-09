#pragma once

#include <sys/epoll.h>
#include <vector>

#include "Poller.h"
#include "Timestamp.h"

class Channel;
class EPollPoller : public Poller {
public:
  EPollPoller(EventLoop *loop);
  ~EPollPoller() override;

  Timestamp poll(int timeoutMs, ChannelList *activeChannels) override;
  void updateChannel(Channel *channel) override;
  void removeChannel(Channel *channel) override;

private:
  static const int kInitEventListSize = 16;
  // 整型（int/char/size_t 等）或枚举类型的 static const
  // 类成员，允许直接在头文件的类定义中初始化（编译期常量），无需在 .cpp
  // 文件中单独定义（除非需要取该常量的地址）。
  //  活跃连接
  void fillActiveChannels(int numEvents, ChannelList *activeChannels) const;

  void update(int operation, Channel *channel);

  using EventList = std::vector<epoll_event>;

  int epollfd_;

  EventList events_;
}
