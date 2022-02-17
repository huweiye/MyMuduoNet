#ifndef _MUDUO_NET_CHANNEL_H_
#define _MUDUO_NET_CHANNEL_H_
#include<muduo/base/Timestamp.h>
#include "noncopyable.h"

#include<memory>
#include<functional>

namespace muduo{
namespace net{
class EventLoop;

class Channel:public noncopyable{
public://tepydef
    typedef std::function<void()> EventCallBackFunc;
    typedef std::function<void(Timestamp)> ReadEventCallBackFunc;

private://数据成员
    EventLoop * loop_;//这个Channel归属于哪个EventLoop管理
    const int  fd_;//该Channel管理的fd
    int        events_;//当前Channel关心的事件
    int revents_;//活跃的事件
    int        index_;//提高Poller效率的，不重要
    bool       logHup_;
    static const int kNoneEvent;
    static const int kReadEvent;
    static const int kWriteEvent;

    //std::weak_ptr<void> tie_;
    //bool tied_;
    bool eventHandling_;//当前Channel是否处于handleEvent()函数中，即是否正在处理事件
    bool addedToLoop_;//当前Channel是否已处于EventLoop中

    //Channel事件响应时分别调用的回调函数：
    ReadEventCallBackFunc readCallback_;
    EventCallBackFunc writeCallback_;
    EventCallBackFunc closeCallback_;
    EventCallBackFunc errorCallback_;

private://私有成员函数
    void update();/**注册或者更新Channel*/

public://共有成员函数
    Channel(EventLoop * loop,int fd);
    ~Channel();

    /**事件回调函数**/ 
    void handleEvent(Timestamp activeTime);

    //对外提供的，TCPConnection或者Acceptor中调用他们注册他们自己的回调函数
    void setReadCallback(ReadEventCallBackFunc cb)
    { readCallback_ = std::move(cb); }
    void setWriteCallback(EventCallBackFunc cb)
    { writeCallback_ = std::move(cb); }
    void setCloseCallback(EventCallBackFunc cb)
    { closeCallback_ = std::move(cb); }
    void setErrorCallback(EventCallBackFunc cb)
    { errorCallback_ = std::move(cb); }


    void enableReading() { events_ |= kReadEvent; update(); }//添加可读事件
    void disableReading() { events_ &= ~kReadEvent; update(); }//取消可读事件
    void enableWriting() { events_ |= kWriteEvent; update(); }//添加可写事件
    void disableWriting() { events_ &= ~kWriteEvent; update(); }//取消可写事件
    void disableAll() { events_ = kNoneEvent; update(); }//取消全部事件监听

    bool isWriting() const { return events_ & kWriteEvent; }
    bool isReading() const { return events_ & kReadEvent; }

    void remove();//把当前Channel从他所属的EventLoop中移除
    int fd() const { return fd_; }
    void set_revents(int revt) { revents_ = revt; } // used by pollers,poller调用此函数为Channel设置活跃的事件
    // for Poller:
    int index() { return index_; }
    void set_index(int idx) { index_ = idx; }
    int interested_events() const { return events_; }
    bool isNoneEvent() const { return events_ == kNoneEvent; }
    void tie(const std::shared_ptr<void>&);
};
}
}
#endif
