#include <muduo/net/PollPoller.h>

#include <muduo/base/Logging.h>
#include <muduo/base/Types.h>
#include <muduo/net/Channel.h>

#include <assert.h>
#include <errno.h>
#include <poll.h>

using namespace muduo;
using namespace muduo::net;

PollPoller::PollPoller(EventLoop* loop)
    : Poller(loop)
{
}
PollPoller::~PollPoller()
{
}
void PollPoller::fillActiveChannels(int numEvents,
                                    ChannelList* activeChannels) const
//根据::poll()返回的活跃事件，填充到activeChannels数组里
{
    for (auto pfd= pollfds_.begin();
         pfd != pollfds_.end() && numEvents > 0; ++pfd)//遍历::poll()返回的活跃事件,挨个设置
    {
        if (pfd->revents > 0)
        {
            --numEvents;
            auto ch = channels_.find(pfd->fd);
            Channel* channel = ch->second;
            channel->set_revents(pfd->revents);/**设置这个Channel上的活跃事件****/
            activeChannels->emplace_back(channel);
        }
    }
    return;
}

/***调用等待IO事件的函数::poll()*****/
Timestamp PollPoller::poll(int timeoutMs, ChannelList* activeChannels)
{
    int numEvents = ::poll(pollfds_.data(), pollfds_.size(), timeoutMs);
    int savedErrno = errno;
    Timestamp now(Timestamp::now());
    if (numEvents > 0)
    {
        LOG_TRACE << numEvents << " events happened";
        /*******设置活跃事件***********/
        fillActiveChannels(numEvents, activeChannels);

    }
    else if (numEvents == 0)
    {
        LOG_TRACE << " nothing happened";
    }
    else
    {
        if (savedErrno != EINTR)
        {
            errno = savedErrno;
            LOG_SYSERR << "PollPoller::poll()";
        }
    }
    return now;
}
/*******注册或者更新参数channel上的IO可读、可写等事件******/
void PollPoller::updateChannel(Channel* channel)
{
    LOG_TRACE << "fd = " << channel->fd() << " events = " << channel->interested_events();
    if (channel->index() < 0)//往poll上添加新fd和事件
    {
        // a new one, add to pollfds_
        struct pollfd pfd;
        pfd.fd = channel->fd();
        pfd.events = static_cast<short>(channel->interested_events());
        pfd.revents = 0;
        pollfds_.emplace_back(pfd);
        int idx = static_cast<int>(pollfds_.size())-1;
        channel->set_index(idx);
        channels_[pfd.fd] = channel;
    }
    else
    {//---------------更新事件-------------
        // update existing one
        int idx = channel->index();
        struct pollfd& pfd = pollfds_[idx];
        pfd.fd = channel->fd();
        pfd.events = static_cast<short>(channel->interested_events());
        pfd.revents = 0;
        if (channel->isNoneEvent())
        {
            // ignore this pollfd
            pfd.fd = -channel->fd()-1;
        }
    }
}
/******Channel从::poll()的监听事件集合中******/
void PollPoller::removeChannel(Channel* channel)
{
    LOG_TRACE << "fd = " << channel->fd();
    int idx = channel->index();
    const struct pollfd& pfd = pollfds_[idx]; (void)pfd;
    channels_.erase(channel->fd());
    if (implicit_cast<size_t>(idx) == pollfds_.size()-1)
    {
        pollfds_.pop_back();
    }
    else
    {
        int channelAtEnd = pollfds_.back().fd;
        iter_swap(pollfds_.begin()+idx, pollfds_.end()-1);
        if (channelAtEnd < 0)
        {
            channelAtEnd = -channelAtEnd-1;
        }
        channels_[channelAtEnd]->set_index(idx);
        pollfds_.pop_back();
    }
}
