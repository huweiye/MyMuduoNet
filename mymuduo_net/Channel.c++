#include<muduo/base/Logging.h>
#include<muduo/net/Channel.h>
#include<muduo/net/EventLoop.h>

#include<sstream>
#include<memory>

#include<sys/epoll.h>

using namespace muduo;
using namespace muduo::net;

const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = EPOLLIN | EPOLLPRI;//等价于POLLIN|POLLPRI，值上二者是相等的
const int Channel::kWriteEvent = EPOLLOUT;//等价于POLLOUT

Channel::Channel(EventLoop * loop,int fd):
    loop_(loop),//当前Channel所属的evetnloop
    fd_(fd),//当前Channel管理的fd
    events_(0),//当前Channel正在监听的事件
    revents_(0),//当前Channel在POller返回后的活跃事件
    index_(-1),
    logHup_(true),
    eventHandling_(false),//当前Channel是否正在处理事件
    addedToLoop_(false)
{

}
Channel::~Channel()//因为Channel不拥有fd或者其他资源，因此析构函数可以什么都不做
{

}

void Channel::update()//将当前Channel的可读可写事件更新到他所属的EventLoop
{
    addedToLoop_ = true;
    loop_->updateChannel(this);
}
void Channel::remove()//将当前Channel从他所属的EventLoop中移除，EventLoop进一步调用Poller上的remove函数
{
    addedToLoop_ = false;
    loop_->removeChannel(this);
}
/**事件回调函数,被Poller的poll()所调用，用于在poll()/epoll()返回时响应活跃事件**/
void Channel::handleEvent(Timestamp activeTime){
    eventHandling_ = true;
    if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN))//EPOLLHUP:不可读也不可写，对端close()
    {
        LOG_WARN << "fd = " << fd_ << " Channel::handle_event() POLLHUP";
        if (closeCallback_) closeCallback_();
    }
    if (revents_ & EPOLLERR )
    {
        if (errorCallback_) errorCallback_();
    }
    if (revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP))//EPOLLRDHUP:不可读，对端调用close()或者shutdown(WR)
    {
        if (readCallback_) readCallback_(activeTime);
    }
    if (revents_ & EPOLLOUT)//可写事件满足
    {
        if (writeCallback_) writeCallback_();
    }
}
