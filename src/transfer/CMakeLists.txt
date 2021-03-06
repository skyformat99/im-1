PROJECT(im_transfer)

CMAKE_MINIMUM_REQUIRED(VERSION 2.8)
set(CMAKE_CXX_STANDARD 11)
include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
CHECK_CXX_COMPILER_FLAG("-std=c++0x" COMPILER_SUPPORTS_CXX0X)
if(COMPILER_SUPPORTS_CXX11)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g  -std=c++11 -pthread -O2")
elseif(COMPILER_SUPPORTS_CXX0X)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -std=c++0x")
else()
    message(STATUS "The compiler ${CMAKE_CXX_COMPILER} has no C++11 support. Please use a different C++ compiler.")
endif()


#set(CMAKE_BUILD_TYPE debug)


AUX_SOURCE_DIRECTORY(. DIR_SRCS)

# dir root
INCLUDE_DIRECTORIES("./core")
FILE(GLOB root "./core/*.cc")

# dir tools
INCLUDE_DIRECTORIES("../tools/")
FILE(GLOB tools "../tools/*.cc")

# dir base
INCLUDE_DIRECTORIES("../tools/base/")
FILE(GLOB base "../tools/base/*.cc")

# dir threadpool
INCLUDE_DIRECTORIES("../tools/threadpool/")

# dir protobuf
INCLUDE_DIRECTORIES("../protobuf/")
FILE(GLOB protobuf "../protobuf/*.cc")

# dir redis
INCLUDE_DIRECTORIES("../redis/")
FILE(GLOB redis "../redis/hiredis/*.c")


#dir log4cpp
INCLUDE_DIRECTORIES("../../include/")


LINK_DIRECTORIES("/usr/local/lib/")
LINK_DIRECTORIES("../../libs/")


ADD_EXECUTABLE(transfer  ${DIR_SRCS} ${root} ${tools} ${base} ${protobuf}  ${redis}  )
TARGET_LINK_LIBRARIES(transfer  protobuf -lpthread log4cpp event_pthreads event jemalloc)
