cmake_minimum_required (VERSION 2.8)#cmake最低版本
project (MyMuduo) #工程名

set (EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin) #生成的可执行文件放到工程目录的bin目录下

aux_source_directory(./testcase SRCLIST) #testcase目录下所有文件

add_subdirectory(mymuduo_net) #执行mymuduo_net下的cmake


add_executable (main ${SRCLIST})

find_library(MUDUONET mymuduo_net HINTS ${PROJECT_SOURCE_DIR}/lib)#寻找并链接mymuduonet库
target_link_libraries (main ${MUDUONET})
