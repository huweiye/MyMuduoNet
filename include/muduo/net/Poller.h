#ifndef _MUDUO_NET_POLLER_H_
#define _MUDUO_NET_POLLER_H_
#include <map>
#include <vector>
#include<memory>

#include <muduo/base/Timestamp.h>
#include <muduo/net/EventLoop.h>
#include "noncopyable.h"
namespace muduo
{
namespace net
{
class Channel;
/**Poller是对IO复用的抽象****/
class Poller:public noncopyable{//虚基类

public:
    typedef std::vector<Channel *> ChannelList;//Poller所管理的Channel的列表
    typedef std::map<int, Channel*> ChannelMap;

private:
    EventLoop* ownerLoop_;//一个Poller被一个EventLoop所拥有

protected:
    ChannelMap channels_;
public:
    Poller(EventLoop* loop);
    virtual ~Poller();

    /***四个被派生类实现的虚函数****/
    /// Polls the I/O events.
    /// Must be called in the loop thread.
    virtual Timestamp poll(int timeoutMs, ChannelList* activeChannels) = 0;//被EventLoop::loop()调用

    /// Changes the interested I/O events.
    /// Must be called in the loop thread.
    virtual void updateChannel(Channel* channel) = 0;//被EventLoop::updateChannel()调用

    /// Remove the channel, when it destructs.
    /// Must be called in the loop thread.
    virtual void removeChannel(Channel* channel) = 0;

    virtual bool hasChannel(Channel* channel) const;

    /**这个函数定义在DefalutPoller.c++中，默认返回指向堆上的EpollPoller的指针**/
    static Poller * newDefaultPoller(EventLoop* loop);//在EventLoop中被调用，生成专属于它的Poller实例

};
}
}
#endif
