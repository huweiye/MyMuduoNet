#include <muduo/net/EPollPoller.h>

#include <muduo/base/Logging.h>
#include <muduo/net/Channel.h>

#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <sys/epoll.h>
#include <unistd.h>

using namespace muduo;
using namespace muduo::net;
namespace
{
const int kNew = -1;
const int kAdded = 1;
const int kDeleted = 2;
}

EPollPoller::EPollPoller(EventLoop* loop)
  : Poller(loop),
    epollfd_(::epoll_create1(EPOLL_CLOEXEC)),
    events_(kInitEventListSize)
{
  if (epollfd_ < 0)
  {
    LOG_SYSFATAL << "EPollPoller::EPollPoller";
  }
}

EPollPoller::~EPollPoller()
{
  ::close(epollfd_);
}

//根据::epoll_wait()返回的活跃事件，填充到activeChannels数组里
void EPollPoller::fillActiveChannels(int numEvents,
                                     ChannelList* activeChannels) const
{
  for (int i = 0; i < numEvents; ++i)
  {
    Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
    channel->set_revents(events_[i].events);
    activeChannels->push_back(channel);
  }
}

/***调用等待IO事件的函数::epoll_wait()*****/
Timestamp EPollPoller::poll(int timeoutMs, ChannelList* activeChannels)
{
  LOG_TRACE << "fd total count " << channels_.size();
  int numEvents = ::epoll_wait(epollfd_,
                               events_.data(),
                               static_cast<int>(events_.size()),
                               timeoutMs);
  int savedErrno = errno;
  Timestamp now(Timestamp::now());
  if (numEvents > 0)
  {
    LOG_TRACE << numEvents << " events happened";
    fillActiveChannels(numEvents, activeChannels);
    if (implicit_cast<size_t>(numEvents) == events_.size())
    {
      events_.resize(events_.size()*2);
    }
  }
  else if (numEvents == 0)
  {
    LOG_TRACE << "nothing happened";
  }
  else
  {
    // error happens, log uncommon ones
    if (savedErrno != EINTR)
    {
      errno = savedErrno;
      LOG_SYSERR << "EPollPoller::poll()";
    }
  }
  return now;
}

/*******注册或者更新参数channel上的IO可读、可写等事件******/
void EPollPoller::updateChannel(Channel* channel)
{
  const int index = channel->index();
  LOG_TRACE << "fd = " << channel->fd()
    << " events = " << channel->interested_events() << " index = " << index;
  if (index == kNew || index == kDeleted)
  {
    // a new one, add with EPOLL_CTL_ADD
    int fd = channel->fd();
    if (index == kNew)
    {
      assert(channels_.find(fd) == channels_.end());
      channels_[fd] = channel;
    }
    else // index == kDeleted
    {
      assert(channels_.find(fd) != channels_.end());
      assert(channels_[fd] == channel);
    }

    channel->set_index(kAdded);
    update(EPOLL_CTL_ADD, channel);
  }
  else
  {
    // update existing one with EPOLL_CTL_MOD/DEL
    int fd = channel->fd();
    (void)fd;
    assert(channels_.find(fd) != channels_.end());
    assert(channels_[fd] == channel);
    assert(index == kAdded);
    if (channel->isNoneEvent())
    {
      update(EPOLL_CTL_DEL, channel);
      channel->set_index(kDeleted);
    }
    else
    {
      update(EPOLL_CTL_MOD, channel);
    }
  }
}
/******Channel从event_poll的监听事件集合中移除******/
void EPollPoller::removeChannel(Channel* channel)
{
  int fd = channel->fd();
  LOG_TRACE << "fd = " << fd;
  assert(channels_.find(fd) != channels_.end());
  assert(channels_[fd] == channel);
  assert(channel->isNoneEvent());
  int index = channel->index();
  assert(index == kAdded || index == kDeleted);
  size_t n = channels_.erase(fd);
  (void)n;
  assert(n == 1);

  if (index == kAdded)
  {
    update(EPOLL_CTL_DEL, channel);
  }
  channel->set_index(kNew);
}

void EPollPoller::update(int operation, Channel* channel)
{
  struct epoll_event event;
  bzero(&event, sizeof event);
  event.events = channel->interested_events();
  event.data.ptr = channel;
  int fd = channel->fd();
  if (::epoll_ctl(epollfd_, operation, fd, &event) < 0)
  {
    if (operation == EPOLL_CTL_DEL)
    {
      LOG_SYSFATAL << "epoll_ctl op =" <<"EPOLL_CTL_DEL"<< " fd =" << fd;
    }
    else
    {
      LOG_SYSFATAL << "epoll_ctl op =" <<"EPOLL_CTL_MOD"<< " fd =" << fd;
    }
  }
}
