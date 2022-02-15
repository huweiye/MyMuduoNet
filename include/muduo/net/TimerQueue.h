#ifndef _MUDUO_NET_TIMERQUEUE_H_
#define _MUDUO_NET_TIMERQUEUE_H_
#include <set>
#include <vector>

#include <muduo/base/Mutex.h>
#include <muduo/base/Timestamp.h>
#include <muduo/net/Channel.h>

namespace muduo
{
namespace net
{

class EventLoop;
class Timer;
class TimerId;
//定时器管理类，持有并管理所有Timer
class TimerQueue : noncopyable{
private:
    //FIX muduo 1.0 version，使用了智能指针shared_ptr取代了裸指针，避免了在析构函数中手动delete
    typedef std::pair<Timestamp, std::shared_ptr<Timer>> Entry;
    typedef std::set<Entry> TimerList;
    typedef std::pair<std::shared_ptr<Timer>, int64_t> ActiveTimer;
    typedef std::set<ActiveTimer> ActiveTimerSet;
    typedef std::function<void()> TimerCallback;

    EventLoop* loop_;//所属的EventLoop
    const int timerfd_;
    Channel timerfdChannel_;
    // Timer list sorted by expiration
    TimerList timers_;

    // for cancel()
    ActiveTimerSet activeTimers_;

    bool callingExpiredTimers_; /* atomic */
    ActiveTimerSet cancelingTimers_;

private:
    void addTimerInLoop(std::shared_ptr<Timer> timer);
    void cancelInLoop(TimerId timerId);
    //tmiefd上有可读事件时回调
    void handleRead();
    //处理并移除所有超时定时器
    std::vector<Entry> getExpired(Timestamp now);

    void reset(const std::vector<Entry>& expired, Timestamp now);

    bool insert(std::shared_ptr<Timer> timer);

public:
    explicit TimerQueue(EventLoop* loop);
    ~TimerQueue();
    TimerId addTimer(TimerCallback cb,//往TimerQueue里添加定时器
                     Timestamp when,
                     double interval);

    void cancel(TimerId timerId);//移除定时器

};
}
}

#endif
