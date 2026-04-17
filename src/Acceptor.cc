#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "Acceptor.h"
#include "InetAddress.h"
#include "Logger.h"

static int createNonblocking() {
  int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                        IPPROTO_TCP);
  // 使用 IPv4 协议族。

  // 对应的地址结构为 struct sockaddr_in
  //<BS>SOCK_STREAM
  // 基础类型，提供顺序、可靠、基于连接的字节流服务（TCP）。

  // SOCK_NONBLOCK
  // 将套接字设置为 非阻塞模式。
  // 后续的 read()、write()、connect()、accept()
  // 等操作若无法立即完成，会立即返回 -1，并设置 errno 为 EAGAIN 或
  // EWOULDBLOCK。 避免了为设置非阻塞而单独调用 fcntl(sockfd, F_SETFL,
  // O_NONBLOCK)，并消除了调用之间的竞争条件。

  // SOCK_CLOEXEC
  // 设置 close-on-exec 标志（FD_CLOEXEC）。
  // 当进程调用 exec() 执行新程序时，该套接字会自动关闭，不会泄露给新程序。
  // 同样避免了单独调用 fcntl(sockfd, F_SETFD, FD_CLOEXEC)
  // 的竞争问题。SOCK_STREAM
  if (sockfd < 0) {
    LOG_FATAL("%s:%s:%d listen socket create err:%d\n", __FILE__, __FUNCTION__,
              __LINE__, errno);
  }
  return sockfd;
}
Acceptor::Acceptor(EventLoop *loop, const InetAddress &listenAddr,
                   bool reuseport)
    : loop_(loop), acceptSocket_(createNonblocking()),
      acceptChannel_(loop, acceptSocket_.fd()), listenning_(false) {
  acceptSocket_.setReuseAddr(true);
  acceptSocket_.setReusePort(true);
  acceptSocket_.bindAddress(listenAddr);
  acceptChannel_.setReadCallback(std::bind(&Acceptor ::handleRead, this));
  // 给 acceptChannel_（封装 listenfd 的 Channel）绑定读事件回调函数
  // 当 listenfd 有新连接到来时（Poller 检测到 EPOLLIN 事件），Channel
  // 会触发该回调，执行 Acceptor::handleRead()； 把「当前 Acceptor
  // 对象」+「handleRead 成员函数」打包成一个可调用的回调函数然后把这个回调交给
  // Channel。 std::bind 绑定类成员函数时：
  // 必须传入对象指针（this），因为成员函数不能独立运行，必须依附对象
}
Acceptor::~Acceptor() {
  acceptChannel_.disableAll(); // 从poller中删掉感兴趣事件
  acceptChannel_.remove();     // 会调用EventLoop_>removechannle,调用
                               // Poller->removechannle
                               // 把poller的channel_map对应部分删掉
}

void Acceptor::listen() {
  listenning_ = true;
  acceptSocket_.listen();
  acceptChannel_.enableReading(); // acceptChannel_注册至Poller
}

// listenfd有事件发生了，就是有新用户连接了
void Acceptor::
    handleRead() { // listenfd（服务器监听套接字）一旦触发读事件，就代表有新的客户端
                   // TCP 连接请求到达。
                   //
  InetAddress peerAddr;
  // peerAddr：用来存储新连接的客户端信息（IP 地址 + 端口号）
  int connfd = acceptSocket_.accept(&peerAddr);
  // acceptSocket_：是服务器的监听 Socket（listenfd）；
  //.accept(&peerAddr)：调用 muduo 封装的accept方法，底层调用 Linux
  // 系统调用::accept4； 执行逻辑： 从内核的TCP
  // 全连接队列中取出一个已完成三次握手的新连接； 返回一个全新的文件描述符
  // connfd（专门用于和这个客户端通信）； 同时把客户端的 IP / 端口写入peerAddr；
  if (connfd >= 0) {
    if (NewConnectionCallback_) {
      // 判断：上层（TcpServer）是否给Acceptor设置了新连接回调函数

      // NewConnectionCallback_是一个std::function回调；
      // 由TcpServer注册给Acceptor，是两者的解耦桥梁。
      NewConnectionCallback_(
          connfd,
          peerAddr); // 轮询找到subLoop 唤醒并分发当前的新客户端的Channel

      // 执行回调：把新连接分发给上层

      // 把新连接的 connfd + 客户端地址 peerAddr 传递给TcpServer；
      // TcpServer拿到后：
      // 轮询选择一个subLoop（子 Reactor）；
      // 把connfd封装成Channel；
      // 交给子线程处理后续的读写数据；
    } else {
      ::close(connfd);
    }
  } else {
    LOG_ERROR("%s:%s:%d accept err:%d\n", __FILE__, __FUNCTION__, __LINE__,
              errno);
    if (errno == EMFILE) {
      LOG_ERROR("%s:%s:%d sockfd reached limit\n", __FILE__, __FUNCTION__,
                __LINE__);
    }
  }
}
/*有新连接 → 触发 handleRead
调用 accept → 拿到客户端的 connfd
通过回调 → 把连接交给 TcpServer
失败 / 无回调 → 关闭 fd，打印错误*/
