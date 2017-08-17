#ifndef _TIME_UTIL_H
#define _TIME_UTIL_H

#include <string>
#include <sys/time.h>
namespace TimeUtil {
    std::string timestamp_datetime();
    int timestamp_int();
	long long get_mstime() {
		struct timeval tv;
		long long mst;

		gettimeofday(&tv, NULL);
		mst = ((long long)tv.tv_sec) * 1000;
		mst += tv.tv_usec / 1000;
		return mst;
	}
}

#endif
