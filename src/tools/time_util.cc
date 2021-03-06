#include "time_util.h"

namespace TimeUtil {
    std::string timestamp_datetime() {
        std::string datetime;
        char datatimetmp[25] = "";
        time_t t;
        tm *tmp;

        t = time(nullptr);
        tmp = localtime(&t);
        strftime(datatimetmp, 24, "%Y-%m-%d %H:%M:%S", tmp);
        datetime = datatimetmp;

        return datetime;
    }

    int timestamp_int() {
        return time(nullptr);
    }
	long long get_mstime() {
		struct timeval tv;
		long long mst;

		gettimeofday(&tv, NULL);
		mst = ((long long)tv.tv_sec) * 1000;
		mst += tv.tv_usec / 1000;
		return mst;
	}

}
