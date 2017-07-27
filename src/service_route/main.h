#ifndef _MAIN_H
#define _MAIN_H

#include "redis_client.h"
#include "threadpool/ThreadPool.h"

#define CONNECTPOOL_AND_THREADPOOL_NUMBER 4 

extern RedisClient redis_client;
extern ThreadPool thread_pool;

#endif
