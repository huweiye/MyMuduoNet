#ifndef _MUDUO_NET_TIMER_H_
#define _MUDUO_NET_TIMER_H_
#include <muduo/base/Atomic.h>
#include <muduo/base/Timestamp.h>
#include "noncopyable.h"
#include<functional>

namespace muduo
{
namespace net
{
//只是对定时器的抽象，比如设置当前定时器到时时要调用的回调函数cb，到期时间，是否重复等
class Timer : noncopyable
{
public:
    typedef std::function<void()> TimerCallback;
 public:
  Timer(TimerCallback cb, Timestamp when, double interval)
    : callback_(std::move(cb)),
      expiration_(when),
      interval_(interval),
      repeat_(interval > 0.0),
      sequence_(s_numCreated_.incrementAndGet())
  { }

  void run() const
  {
    callback_();
  }

  Timestamp expiration() const  { return expiration_; }
  bool repeat() const { return repeat_; }
  int64_t sequence() const { return sequence_; }

  void restart(Timestamp now);

  static int64_t numCreated() { return s_numCreated_.get(); }

 private:
  const TimerCallback callback_;
  Timestamp expiration_;
  const double interval_;
  const bool repeat_;
  const int64_t sequence_;

  static AtomicInt64 s_numCreated_;
};
}
}
#endif
