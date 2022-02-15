#include <muduo/net/Poller.h>
#include <muduo/net/PollPoller.h>
#include <muduo/net/EPollPoller.h>

#include <stdlib.h>
#include<memory>

using namespace muduo::net;

Poller *Poller::newDefaultPoller(EventLoop* loop)//参数：这个Poller属于哪个EventLoop
    //返回Poller的某个派生类对象，默认使用EPOllPoller
{
    if (::getenv("MUDUO_USE_POLL"))
    {
        return new PollPoller(loop);
    }
    else//--------------default:
    {
        return new EPollPoller(loop);
    }
}

