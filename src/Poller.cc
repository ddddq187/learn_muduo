#include "Poller.h"
#include "Channel.h"

Poller::Poller(EventLoop *loop) : ownerLoop_(loop) {}
