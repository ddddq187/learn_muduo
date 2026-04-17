#include <functional>
#include <string.h>
#include <string>
#include <unistd.h>

#include "Acceptor.h"
#include "EventLoop.h"
#include "Logger.h"
#include "TcpConnection.h"
#include "TcpServer.h"

static EventLoop *CheckLoopNotNull(EventLoop *loop) {
  if (loop == nullptr) {
    LOG_FATAL("%s:%s:%d mainLoop is null!\n", __FILE__, __FUNCTION__, __LINE__);
  }
  return loop;
}
TcpServer::TcpServer(EventLoop *loop, const InetAddress &listenAddr,
                     const std::string &nameArg, Option option)
    : loop_(CheckLoopNotNull(loop)), ipPort_(listenAddr.toIpPort()),
      name_(nameArg),
      acceptor_(new Acceptor(loop, listenAddr, option == kReusePort)),
      threadPool_(new EventLoopThreadPool(loop, name_)), connectionCallback_(),
      messageCallback_(), nextConnId_(1), started_(0) {
  // 新用户连接时，Acceptor中的类acceptChannel会有读事件发生
  // 执行handleRead()调用TcpServer：：newConnection回调
  acceptor_->setNewConnectionCallback(std::bind(&Tcpserver::newConnection, this,
                                                std::placeholders::_1,
                                                std::placeholders::_2));
}

TcpServer::~TcpServer() {
  for (auto &item : connections_) {

    TcpConnectionPtr conn(item.second);
    // 这里相当于引用计数加一，防止reset后连接直接被释放
    // TcpConnection 只能在它所属的 EventLoop 线程中销毁！
    // 不能在 TcpServer 主线程直接销毁！
    item.second.reset();

    conn->getLoop()->runInLoop(
        std::bind(&TcpConnection::connectDestyoyed, conn));
  }
}

// 设置底层subloop的个数
void TcpServer::setThreadNum(int numThreads) {
  int numThreads_ = numThreads;
  threadPool_->setThreadNum(numThreads_);
}

// 开启服务器监听
void TcpServer::start() {
  if (started_.fetch_add(1) == 0) { // 防止一个tcpSercer被start多次
    // 自增1返回自增前的值，初始化为0则只有第一次调用才进if
    threadPool_->start(threadInitCallback_); // 启动底层loop
    // 创建N个线程，每个线程运行一个EventLoop：：loop
    loop_->runInLoop(std::bind(&Acceptor::listen, acceptor_.get()));
    // loop_是主线程，值接收新连接不读写
    // start可能被其他线程使用
    // runInLoop保证在loop_线程，然后分发任务
  }
}

void TcpServer::removeConnection(const TcpConnectionPtr &conn) {
  loop_->runInLoop(std::bind(&TcpServer::removeConnectionInLoop, this, conn));
  // 第 1 个参数：函数地址
  // 第 2 个参数：对象指针（this）
  // 第 3 个开始：函数需要的参数
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr &conn) {
  LOG_INFO("TcpServer::removeConnectionInLoop [%s] - connection %s\n",
           name_.c_str(), conn->name().c_str());
  connections_.erase(conn->name());    // 从TcpServer的管理范围中删掉此连接
  EventLoop *ioLoop = conn->getLoop(); // 获取TcpConnection所属的subLoop
  ioLoop->queueInLoop(std::bind(&TcpConnection::connectDestroyed,
                                conn)); // 投递销毁任务到 subLoop 队列
}

// 有一个新用户连接，acceptor会执行这个回调操作，负责将mainLoop接收到的请求连接(acceptChannel_会有读事件发生)通过回调轮询分发给subLoop去处理
void TcpServer::newCon nection(int sockfd, const InetAddress &peerAddr) {

  // 轮询
  EventLoop *ioLoop =
      threadPool_->getNextLoop(); // getNextLoop()：轮询算法，依次返回线程池中的
                                  // subLoop，实现负载均衡；
  char buf[64] = {0};
  snprintf(buf, sizeof buf, "-%s#%d", ipPort_.c_str(), nextConnId_);
  ++nextConnId_;
  std::string connName = name_ + buf; // 全局唯一连接名称，只在 mainLoop
                                      // 单线程执行，没有线程竞争，无需原子操作
  LOG_INFO("TcpServer::newConnection [%s] - new connection [%s] from %s\n",
           name_.c_str(), connName.c_str(), peerAddr.toIpPort().c_str());
  // 通过sockfd得到本机ip和端口
  sockaddr_in local;
  ::menset(&local, 0, sizeof(local));
  socklen_t addrlen = sizeof(local);
  if (::getsockname(sockfd, (sockaddr *)&local, &addrlen) < 0) {
    LOG_ERROR("sockets::getLocalAddr"); // getsockname()：Linux 系统调用，获取
                                        // sockfd 绑定的本机 IP + 端口；
  }
  InetAddress loaclAddr(local);
  TcpConnectionPtr conn(
      new TcpConnection(ioLoop, connName, sockfd, localAddr, peerAddr));
  connections_[connName] =
      conn; // TcpConnection 是 muduo 对一个客户端连接的完整封装：
            // 所属 subLoop：ioLoop
  // 连接名称：connName
  // 通信套接字：sockfd
  // 本机地址：localAddr
  // 客户端地址：peerAddr
  //  cb设置给TcpServer => TcpConnection
  //  Channel绑定的则是TcpConnection设置的四个，handleRead,handleWrite...
  //  这下面的回调用于handlexxx函数中
  conn->setConnectionCallback(connectionCallback_);
  conn->setMessageCallback(messageCallback_);
  conn->setWriteCompleteCallback(writeCompleteCallback_);

  // 设置了如何关闭连接的回调
  conn->setCloseCallback(
      std::bind(&TcpServer::removeConnection, this, std::placeholders::_1));

  ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));
}
