#ifndef _MAIN_H
#define _MAIN_H

#include "connection_server.h"
#include "redis_client.h"
#include "ThreadPool.h"

#define CONNECTPOOL_AND_THREADPOOL_NUMBER 4 


extern ConnectionServer connect_server;
extern RedisClient redis_client;
extern ThreadPool thread_pool;

#endif
