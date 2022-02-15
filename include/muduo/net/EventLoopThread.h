#ifndef MUDUO_NET_EVENTLOOPTHREAD_H
#define MUDUO_NET_EVENTLOOPTHREAD_H

#include <muduo/base/Condition.h>
#include <muduo/base/Mutex.h>
#include <muduo/base/Thread.h>
#include "noncopyable.h"

namespace muduo
{
namespace net
{

class EventLoop;

//对创建使用EventLoop对象的线程封装
class EventLoopThread : noncopyable
{
 public:
  typedef std::function<void(EventLoop*)> ThreadInitCallback;

  EventLoopThread(const ThreadInitCallback& cb = ThreadInitCallback(),
                  const string& name = string());
  ~EventLoopThread();
  EventLoop* startLoop();
//startLoop->threadFunc()->Thread::start()
 private:
  void threadFunc();//注册给Thread对象

  EventLoop* loop_;
  bool exiting_;
  Thread thread_;//基于对象编程思想，包含一个Thread对象
  MutexLock mutex_;
  Condition cond_;
  ThreadInitCallback callback_;
};

}
}

#endif
