#ifndef _MUDUO_NET_POLLER_POLLPOLLER_H_
#define _MUDUO_NET_POLLER_POLLPOLLER_H_
#include <muduo/net/Poller.h>

#include <vector>
#include<poll.h>
namespace muduo
{
namespace net
{
class PollPoller : public Poller
{
public:
  typedef std::vector<struct pollfd> PollFdList;//struct pollfd:man poll
 public:

  PollPoller(EventLoop* loop);
  virtual ~PollPoller();

  virtual Timestamp poll(int timeoutMs, ChannelList* activeChannels);
  virtual void updateChannel(Channel* channel);
  virtual void removeChannel(Channel* channel);

 private:
  //根据活跃事件设置对应的Channel
  void fillActiveChannels(int numEvents,
                          ChannelList* activeChannels) const;

  PollFdList pollfds_;
};
}
}
#endif
