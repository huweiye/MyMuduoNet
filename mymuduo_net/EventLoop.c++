#include <signal.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include<muduo/net/SocketOps.h>
#include <muduo/net/EventLoop.h>
#include <muduo/base/Logging.h>
#include <muduo/base/Mutex.h>
#include <muduo/net/Channel.h>
#include <muduo/net/Poller.h>

#include <algorithm>

using namespace muduo;
using namespace muduo::net;

namespace
{
__thread EventLoop* t_loopInThisThread = 0;

const int kPollTimeMs = 10000;

int createEventfd()
{
  int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (evtfd < 0)
  {
    LOG_SYSERR << "Failed in eventfd";
    abort();
  }
  return evtfd;
}

#pragma GCC diagnostic ignored "-Wold-style-cast"
class IgnoreSigPipe
{
 public:
  IgnoreSigPipe()
  {
    ::signal(SIGPIPE, SIG_IGN);//忽略SIGPIPE信号是必要的
    //client调用 close ，close 会立马发送一个 FIN，server会read到0。但是仅仅从 FIN 数据包上，无法断定对端是 close 还是仅仅 shutdown(SHUT_WR) 半关闭。
    //此时如果server往对端发送数据，若对端已经 close()，对端会回复 RST.
    //如果server向某个已收到RST的套接字继续执行写操作时，此时内核向server进程发送一个SIGPIPE信号，
    //该信号的默认行为是终止进程
  }
};
#pragma GCC diagnostic error "-Wold-style-cast"
class IgnoreSigChild{
public:
    IgnoreSigChild(){::signal(SIGCHLD,SIG_IGN);}
};

IgnoreSigPipe initObj;
}

/****EventLoop的实现******/
EventLoop::EventLoop()
  : looping_(false),
    quit_(false),
    eventHandling_(false),
    callingPendingFunctors_(false),//是否正在调用任务处理
    iteration_(0),//事件循环次数
    threadId_(CurrentThread::tid()),
    poller_(Poller::newDefaultPoller(this)),////一个EventLoop始终持有一个Poller对象
    timerQueue_(new TimerQueue(this)),////定时器集合，一个EventLoop一个TimerQueue
    wakeupFd_(createEventfd()),////就是eventfd,实现线程唤醒,其他线程通过往loop.eventfd里写数据来唤醒当前线程
    wakeupChannel_(new Channel(this, wakeupFd_)),////eventfd对应的Channel,这个特殊的Channel的生存周期归当前eventloop对象管理
    currentActiveChannel_(NULL)
{
  LOG_DEBUG << "EventLoop created " << this << " in thread " << threadId_;
  if (t_loopInThisThread)
  {
    LOG_FATAL << "Another EventLoop " << t_loopInThisThread
              << " exists in this thread " << threadId_;
  }
  else
  {
    t_loopInThisThread = this;
  }
  wakeupChannel_->setReadCallback(
      std::bind(&EventLoop::handleRead, this));//wakeupFd_的读事件处理函数
  // we are always reading the wakeupfd
  wakeupChannel_->enableReading();//wakeupFd_可读，说明有其他线程唤醒我
}

EventLoop::~EventLoop()
{
  LOG_DEBUG << "EventLoop " << this << " of thread " << threadId_
            << " destructs in thread " << CurrentThread::tid();
  wakeupChannel_->disableAll();
  wakeupChannel_->remove();
  ::close(wakeupFd_);
  t_loopInThisThread = NULL;
}
/**************************核心成员函数：事件循环loop()*****************************************/
void EventLoop::loop()
{
  assert(!looping_);
  assertInLoopThread();
  looping_ = true;
  quit_ = false;  // FIXME: what if other threads calls quit() before loop() ?
  LOG_TRACE << "EventLoop " << this << " start looping";

  while (!quit_)
  {
    activeChannels_.clear();
    
    pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);//poll() OR epoll()

    ++iteration_;
    if (Logger::logLevel() <= Logger::TRACE)
    {
      printActiveChannels();
    }
    // TODO sort channel by priority
    eventHandling_ = true;
    for (Channel* channel : activeChannels_)
    {
      currentActiveChannel_ = channel;
      currentActiveChannel_->handleEvent(pollReturnTime_);
    }
    currentActiveChannel_ = NULL;
    eventHandling_ = false;
    doPendingFunctors();
  }
  LOG_TRACE << "EventLoop " << this << " stop looping";
  looping_ = false;
}
/********************管理Channel的成员函数，它们会进一步调用Poller对象的成员函数*********************/
void EventLoop::updateChannel(Channel* channel)
{
  assertInLoopThread();
  poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel* channel)
{
  assertInLoopThread();
  if (eventHandling_)
  {
    assert(currentActiveChannel_ == channel ||
        std::find(activeChannels_.begin(), activeChannels_.end(), channel) == activeChannels_.end());
  }
  poller_->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel* channel)
{
  assertInLoopThread();
  return poller_->hasChannel(channel);
}

/**************************跨线程异步调用的成员函数：*****************************************/
void EventLoop::wakeup()//往eventfd里写
{
  uint64_t one = 1;
  ssize_t n = sockets::write(wakeupFd_, &one, sizeof one);
  if (n != sizeof one)
  {
    LOG_ERROR << "EventLoop::wakeup() writes " << n << " bytes instead of 8";
  }
}
void EventLoop::handleRead()//从eventfd读，避免LT模式下一直触发
{
  uint64_t one = 1;
  ssize_t n = sockets::read(wakeupFd_, &one, sizeof one);
  if (n != sizeof one)
  {
    LOG_ERROR << "EventLoop::handleRead() reads " << n << " bytes instead of 8";
  }
}

//供其他线程调用以退出当前IO线程
void EventLoop::quit()
{
  quit_ = true;//这样loop()中下次就能退出while循环，进而退出loop和它的线程执行函数，也就退出线程
  // There is a chance that loop() just executes while(!quit_) and exits,
  // then EventLoop destructs, then we are accessing an invalid object.
  // Can be fixed using mutex_ in both places.
  if (!isInLoopThread())
  {
    wakeup();
  }
}
//供其他（或自己）调用，以在下次poll/epoll之前先执行一些回调函数，比如添加定时器事件
void EventLoop::runInLoop(Functor cb)
{
  if (isInLoopThread())//是IO线程，直接执行
  {
    cb();
  }
  else
  {
    queueInLoop(std::move(cb));//不是拥有当前loop的线程，加入任务队列
  }
}
void EventLoop::queueInLoop(Functor cb)
{
  {
  MutexLockGuard lock(mutex_);//上锁，因为pendingFunctors_是临界资源，所有线程都可以给本线程添加任务,好处在于给这一处上锁，其他临界资源都不需要上锁了，比如TimerQueue 
  pendingFunctors_.push_back(std::move(cb));
  }

  if (!isInLoopThread() || callingPendingFunctors_)
  {
    wakeup();
  }
}
size_t EventLoop::queueSize() const
{
  MutexLockGuard lock(mutex_);
  return pendingFunctors_.size();
}
//从队列取任务做任务
void EventLoop::doPendingFunctors()
{
  std::vector<Functor> functors;
  callingPendingFunctors_ = true;

  {
  MutexLockGuard lock(mutex_);//必须上锁，因为这个任务队列是临界资源，好处在于给这一处上锁，其他临界资源都不需要上锁了，比如TimerQueue
  functors.swap(pendingFunctors_);
  }

  for (size_t i = 0; i < functors.size(); ++i)
  {
    functors[i]();
  }
  callingPendingFunctors_ = false;
}

/****************Timer添加与取消函数：******************/
TimerId EventLoop::runAt(Timestamp time, TimerCallback cb)
{
  return timerQueue_->addTimer(std::move(cb), time, 0.0);//TimerQueue的addTimer调用的是当前loop的runInloop()，所以是线程安全的
}

TimerId EventLoop::runAfter(double delay, TimerCallback cb)
{
  Timestamp time(addTime(Timestamp::now(), delay));
  return runAt(time, std::move(cb));
}

TimerId EventLoop::runEvery(double interval, TimerCallback cb)
{
  Timestamp time(addTime(Timestamp::now(), interval));
  return timerQueue_->addTimer(std::move(cb), time, interval);
}

void EventLoop::cancel(TimerId timerId)
{
  return timerQueue_->cancel(timerId);
}

//一些辅助函数：省略了muduo库中原有的一些打印日志函数
void EventLoop::abortNotInLoopThread()
{
  LOG_FATAL << "EventLoop::abortNotInLoopThread - EventLoop " << this
            << " was created in threadId_ = " << threadId_
            << ", current thread id = " <<  CurrentThread::tid();
}
//结束
