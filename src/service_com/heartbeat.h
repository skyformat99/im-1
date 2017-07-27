
#ifndef _HEARTBEAT_H
#define _HEARTBEAT_H
#include <array>
#include <set>
#include <mutex>
#define   HEARTBEAT_TIME         180
#define   HEARTBEAT_CHECK_TIME   2


class HeartBeat {
public:
	HeartBeat();
	~HeartBeat();
	void setUserid(int user_id);
private:
	std::array<std::set<int>, (2 * HEARTBEAT_TIME) / HEARTBEAT_CHECK_TIME>  m_users;
	int                                                                     m_index;
	int                                                                     m_expire;
	std::recursive_mutex                                                    m_mutex;
};
#endif