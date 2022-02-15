set(MYMUDUONET_SRC_LIST 
    ./Channel.c++
    ./DefaultPoller.c++
    ./EventLoop.c++
    ./Poller.c++
    ./PollPoller.c++
    ./EPollPoller.c++
    ./EventLoopThread.c++

    ./Timer.c++
    ./TimerQueue.c++

    ./SocketsOps.c++
    ./Socket.c++
    ./InetAddress.c++

    ./Acceptor.c++

    )
set(LIBRARY_OUTPUT_PATH ../lib)
add_compile_options(-g -Wall -std=c++11)

#指定头文件目录
include_directories(../include/net ../include/base)
#需要base库
find_library(MUDUO_BASE muduo_base HINTS ../lib)

#生成静态库
add_library(mymuduo_net STATIC ${MYMUDUONET_SRC_LIST})

target_link_libraries (mymuduo_net ${MUDUO_BASE})

