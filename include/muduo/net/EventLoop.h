#ifndef _MUDUO_NET_EVENTLOOP_H_
#define _MUDUO_NET_EVENTLOOP_H_
#include <atomic>
#include <functional>
#include <vector>

#include <boost/any.hpp>

#include "muduo/base/Mutex.h"
#include "muduo/base/CurrentThread.h"
#include "muduo/base/Timestamp.h"
#include "muduo/net/TimerId.h"

#include "TimerQueue.h"
#include"noncopyable.h"

namespace muduo{
namespace net{

class Channel;
class Poller;
class TimerQueue;

/// Reactor, at most one per thread.
class EventLoop:noncopyable{//对事件循环的抽象，管理一组Channel的IO并分发计算任务
public:
    typedef std::function<void()> Functor;//队列pendinfFunctors的数据成员，每个Functor对象是当前IO线程要执行的一个任务
    typedef std::function<void()> TimerCallback;//Timer到期回调的处理函数
private://私有数据成员
    bool looping_; /* atomic */
    std::atomic<bool> quit_;
    bool eventHandling_; /* atomic */
    bool callingPendingFunctors_; /* atomic */
    int64_t iteration_;
    const pid_t threadId_;//(*this)所属的线程真实id
    Timestamp pollReturnTime_;
    std::unique_ptr<Poller> poller_;//一个EventLoop始终持有一个Poller
    std::unique_ptr<TimerQueue> timerQueue_;//这个reactor的定时器集合，一个EventLoop一个TimerQueue
    int wakeupFd_;//就是eventfd,实现线程唤醒,其他线程通过往loop.eventfd里写数据来唤醒持有这个loop的线程
    std::unique_ptr<Channel> wakeupChannel_;//eventfd对应的Channel,这个特殊的Channel的生存周期归当前eventloop对象管理

    // scratch variables
    std::vector<Channel *> activeChannels_;
    Channel* currentActiveChannel_;

    mutable MutexLock mutex_;

    std::vector<Functor> pendingFunctors_; // @BuardedBy mutex_
private://私有成员函数
    void abortNotInLoopThread();
    void handleRead();  // waked up
    void doPendingFunctors();

    void printActiveChannels() const; // DEBUG

public://共有成员函数
    EventLoop();
    ~EventLoop();  // force out-line dtor, for std::unique_ptr members.

    /// Loops forever.
    ///
    /// Must be called in the same thread as creation of the object.
    void loop();

    /// Quits loop.
    ///
    /// This is not 100% thread safe, if you call through a raw pointer,
    /// better to call through shared_ptr<EventLoop> for 100% safety.
    void quit();

    //通过eventfd唤醒当前正在阻塞在poll的线程
    void wakeup();

    /***实现安全地跨线程调用函数****/
    /// Runs callback immediately in the loop thread.
    /// It wakes up the loop, and run the cb.
    /// If in the same loop thread, cb is run within the function.
    /// Safe to call from other threads.
    void runInLoop(Functor cb);

    /// Queues callback in the loop thread.
    /// Runs after finish pooling.
    /// Safe to call from other threads.
    void queueInLoop(Functor cb);

    size_t queueSize() const;

    /******处理Timer的成员函数：*******/
    /// Time when poll returns, usually means data arrival.
    Timestamp pollReturnTime() const { return pollReturnTime_; }

    /// Runs callback at 'time'.
    /// Safe to call from other threads.
    TimerId runAt(Timestamp time, TimerCallback cb);

    /// Runs callback after @c delay seconds.
    /// Safe to call from other threads.
    TimerId runAfter(double delay, TimerCallback cb);

    /// Runs callback every @c interval seconds.
    /// Safe to call from other threads.
    TimerId runEvery(double interval, TimerCallback cb);

    /// Cancels the timer.
    /// Safe to call from other threads.
    void cancel(TimerId timerId);

    /***用来管理Channel的成员函数*******/
    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);
    bool hasChannel(Channel* channel);

    bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); }

    void assertInLoopThread()
    {
        if (!isInLoopThread())
        {
            abortNotInLoopThread();
        }
    }


    bool eventHandling() const { return eventHandling_; }


    static EventLoop* getEventLoopOfCurrentThread();


};

}

}
#endif
