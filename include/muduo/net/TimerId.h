#ifndef _MUDUO_NET_TIMERID_H_
#define _MUDUO_NET_TIMERID_H_
#include <muduo/base/copyable.h>

#include<memory>
#include<cstdlib>

namespace muduo
{
namespace net
{

class Timer;

///
/// An opaque identifier, for canceling Timer.
///
class TimerId : public muduo::copyable
{
 public:
  TimerId()
    : timer_(NULL),
      sequence_(0)
  {
  }

  TimerId(std::shared_ptr<Timer> timer, int64_t seq)
    : timer_(timer),
      sequence_(seq)
  {
  }

  // default copy-ctor, dtor and assignment are okay

  friend class TimerQueue;

 private:
  std::shared_ptr<Timer> timer_;
  int64_t sequence_;
};

}
}
#endif
