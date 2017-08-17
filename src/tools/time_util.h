#ifndef _TIME_UTIL_H
#define _TIME_UTIL_H

#include <string>
#include <sys/time.h>
namespace TimeUtil {
    std::string timestamp_datetime();
    int timestamp_int();
	long long get_mstime(); 
}

#endif
