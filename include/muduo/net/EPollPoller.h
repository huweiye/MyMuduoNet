#ifndef _MUDUO_NET_POLLER_EPOLLER_H_
#define _MUDUO_NET_POLLER_EPOLLER_H_
#include <muduo/net/Poller.h>

#include <vector>

#include<sys/epoll.h>

namespace muduo
{
namespace net
{
/// IO Multiplexing with epoll(4).
class EPollPoller : public Poller{//对epoll的封装，实现Poller的虚函数
public:
    typedef std::vector<epoll_event> EventList;//struct epoll_event:man epoll_wait
private:
    int epollfd_;
    EventList events_;//用于epoll_wait的传入传出参数，保存内核响应的事件集合

    static const int kInitEventListSize = 64;
    void fillActiveChannels(int numEvents,
                            ChannelList* activeChannels) const;
    void update(int operation, Channel* channel);

public:
    EPollPoller(EventLoop *loop);
    virtual ~EPollPoller();

    virtual Timestamp poll(int timeoutMs, ChannelList* activeChannels);
    virtual void updateChannel(Channel* channel);
    virtual void removeChannel(Channel* channel);
};
}
}
#endif
