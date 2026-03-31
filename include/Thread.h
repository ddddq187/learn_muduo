#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <unistd.h>

#include "noncopyable.h"

class Thread : noncopyable {
public:
  using ThreadFunc = std::function<void()>;
  //  对所有「无参数、无返回值」的可调用对象做了统一封装

  explicit Thread(ThreadFunc, const std::string &name = std::string());
  // 带默认参数空字符
  ~Thread();

  void start();
  void join();

  bool started() { return started_; }
  pid_t tid() const { return tid_; }
  const std::string &name() const { return name_; }
  // 俩const+引用保证了无拷贝和封装性
  // 前一个const限制返回的name_引用为只读
  // 后一个const承诺此函数不修改成员
  static int numCreated() { return numCreated_; }

private:
  void setDefaultName();

  bool started_;
  bool joined_;
  std::shared_ptr<std::thread> thread_;
  // unique更好
  pid_t tid_;       // 在线程创建时再绑定
  ThreadFunc func_; // 线程回调函数
  std::string name_;
  static std::atomic_int numCreated_;
  // 所有类的实例用一个计数器
};
