#include <muduo/net/TimerQueue.h>
#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/Timer.h>
#include <muduo/net/TimerId.h>

#include <sys/timerfd.h>
#include <unistd.h>

namespace muduo
{
namespace net
{
namespace detail
{

//自定义时间戳类型Timestamp和struct timespec等数据结构的交互，不必关心
int createTimerfd()
{
  int timerfd = ::timerfd_create(CLOCK_MONOTONIC,
                                 TFD_NONBLOCK | TFD_CLOEXEC);
  if (timerfd < 0)
  {
    LOG_SYSFATAL << "Failed in timerfd_create";
  }
  return timerfd;
}

struct timespec howMuchTimeFromNow(Timestamp when)
{
  int64_t microseconds = when.microSecondsSinceEpoch()
                         - Timestamp::now().microSecondsSinceEpoch();
  if (microseconds < 100)
  {
    microseconds = 100;
  }
  struct timespec ts;
  ts.tv_sec = static_cast<time_t>(
      microseconds / Timestamp::kMicroSecondsPerSecond);
  ts.tv_nsec = static_cast<long>(
      (microseconds % Timestamp::kMicroSecondsPerSecond) * 1000);
  return ts;
}

void readTimerfd(int timerfd, Timestamp now)
{
  uint64_t howmany;
  ssize_t n = ::read(timerfd, &howmany, sizeof howmany);
  LOG_TRACE << "TimerQueue::handleRead() " << howmany << " at " << now.toString();
  if (n != sizeof howmany)
  {
    LOG_ERROR << "TimerQueue::handleRead() reads " << n << " bytes instead of 8";
  }
}

void resetTimerfd(int timerfd, Timestamp expiration)
{
  // wake up loop by timerfd_settime()
  struct itimerspec newValue;
  struct itimerspec oldValue;
  bzero(&newValue, sizeof newValue);
  bzero(&oldValue, sizeof oldValue);
  newValue.it_value = howMuchTimeFromNow(expiration);
  int ret = ::timerfd_settime(timerfd, 0, &newValue, &oldValue);
  if (ret)
  {
    LOG_SYSERR << "timerfd_settime()";
  }
}

}
}
}

using namespace muduo;
using namespace muduo::net;
using namespace muduo::net::detail;
//-------------------TimerQueue的实现：-------------------
TimerQueue::TimerQueue(EventLoop* loop)
  : loop_(loop), //当前TimerQueue所属的EventLoop
    timerfd_(createTimerfd()),//每个TimerQueue对应一个timerfd
    timerfdChannel_(loop, timerfd_),//对应一个Channel
    timers_(),//std::set , 根据到期时间排好序的Timers
    activeTimers_(),
    callingExpiredTimers_(false)
{
  timerfdChannel_.setReadCallback(
      std::bind(&TimerQueue::handleRead, this));//设置timerfd可读的回调函数，其内容时处理每个到期Timer
  // we are always reading the timerfd, we disarm it with timerfd_settime.
  timerfdChannel_.enableReading();
}

TimerQueue::~TimerQueue()
{
  timerfdChannel_.disableAll();
  timerfdChannel_.remove();
  ::close(timerfd_);
  //因为timers_和ActiveTimers_中的Timer都使用了shared_ptr，因此不需要手动delete
}

//--------------------TimerQueue的接口1，添加定时器
TimerId TimerQueue::addTimer(TimerCallback cb,
                             Timestamp when,
                             double interval)
{
    //构造一个Timer对象，并使用智能指针指向它
    std::shared_ptr<Timer> timer=std::make_shared<Timer>(std::move(cb), when, interval);
    /*****线程安全的异步调用，
     * 当前线程t1把当前定时器Timer加入到线程t2的任务队列中，其中t2是loop对象所在的IO线程
     * 这样t2异步的调用传进去的回调函数：addTimerInLoop()
     * 其中t1,t2可能不相等
     * 这避免了对临界资源---timers_的锁的使用
     * 因为最终操作timers_的都是loop所在的那个IO线程，实现了线程同步********/
  loop_->runInLoop(
      std::bind(&TimerQueue::addTimerInLoop, this, timer));//注册异步调用的回调函数
  return TimerId(timer, timer->sequence());
}
//------------------------TimerQueue的接口2：取消指定id的定时器
void TimerQueue::cancel(TimerId timerId)
{
  loop_->runInLoop(
      std::bind(&TimerQueue::cancelInLoop, this, timerId));
}
//------------------------内部函数实现：

//把addTimer new出来的Timer添加进loop事件循环中
//如果当前线程不是拥有loop的线程，那就是由loop所在的IO线程在loop()中调用，否则就是由当前线程通过addTimer()
void TimerQueue::addTimerInLoop(std::shared_ptr<Timer> timer)
{
  bool earliestChanged = this->insert(timer);

  if (earliestChanged)
  {
    resetTimerfd(timerfd_, timer->expiration());
  }
}
//insert:把传进来的Timer添加进timers_
bool TimerQueue::insert(std::shared_ptr<Timer>timer)
{
  assert(timers_.size() == activeTimers_.size());
  bool earliestChanged = false;
  Timestamp when = timer->expiration();
  TimerList::iterator it = timers_.begin();
  if (it == timers_.end() || when < it->first)
  {
    earliestChanged = true;
  }
  {
       timers_.emplace(Entry(when, timer));
  }
  {
       activeTimers_.emplace(ActiveTimer(timer, timer->sequence()));
  }

  assert(timers_.size() == activeTimers_.size());
  return earliestChanged;
}
//移除Timer
//如果当前线程不是拥有loop的线程，那就是由loop所在的IO线程在loop()中调用，否则就是由当前线程通过addTimer()
void TimerQueue::cancelInLoop(TimerId timerId)
{
  ActiveTimer timer(timerId.timer_, timerId.sequence_);
  ActiveTimerSet::iterator it = activeTimers_.find(timer);
  if (it != activeTimers_.end())
      //先判断要删除的Timer是否处于loop监听
  {
    timers_.erase(Entry(it->first->expiration(), it->first));
    activeTimers_.erase(it);
    //不需要delete Timer资源，因为两个集合里都erase掉了对应的智能指针，不再有指针指向它就没了
  }
  else if (callingExpiredTimers_)
  {
    cancelingTimers_.insert(timer);
  }
}
std::vector<TimerQueue::Entry> TimerQueue::getExpired(Timestamp now)
{
  std::vector<Entry> expired;
  /****因为智能指针重载了比较运算符
   * ，所以就像普通指针运用比较运算一样，智能指针比较大小也是比的包含的原始指针的地址的高低
   * 所以这里不需要重载set的比较函数****/
  Entry sentry(now, std::shared_ptr<Timer>(reinterpret_cast<Timer*>(UINTPTR_MAX)));
  //先比较时间再比较地址，end是第一个严格大于now的定时器的位置
  TimerList::iterator end = timers_.lower_bound(sentry);//end之前就是所有已到期定时器，下面遍历他们
  std::copy(timers_.begin(), end, back_inserter(expired));
  timers_.erase(timers_.begin(), end);

  for (std::vector<Entry>::iterator it = expired.begin();
      it != expired.end(); ++it)
  {
    ActiveTimer timer(it->second, it->second->sequence());
    activeTimers_.erase(timer);
  }

  return expired;
}

void TimerQueue::reset(const std::vector<Entry>& expired, Timestamp now)
{
  Timestamp nextExpire;

  for (std::vector<Entry>::const_iterator it = expired.begin();
      it != expired.end(); ++it)
  {
    ActiveTimer timer(it->second, it->second->sequence());
    if (it->second->repeat()
        && cancelingTimers_.find(timer) == cancelingTimers_.end())
    {
      it->second->restart(now);
      insert(it->second);
    }
    else
    {
    }
  }

  if (!timers_.empty())
  {
    nextExpire = timers_.begin()->second->expiration();
  }

  if (nextExpire.valid())
  {
    resetTimerfd(timerfd_, nextExpire);
  }
}

//----------------------timerfd可读的回调函数：
void TimerQueue::handleRead()
{
  Timestamp now(Timestamp::now());
  readTimerfd(timerfd_, now);

  std::vector<Entry> expired = getExpired(now);

  callingExpiredTimers_ = true;
  cancelingTimers_.clear();
  // safe to callback outside critical section
  for (std::vector<Entry>::iterator it = expired.begin();
      it != expired.end(); ++it)
  {
    it->second->run();
  }
  callingExpiredTimers_ = false;

  reset(expired, now);
}
