#include "heartbeat.h"

HeartBeat::HeartBeat()
{
	m_expire = (HEARTBEAT_TIME / 2) / HEARTBEAT_CHECK_TIME;
}

HeartBeat::~HeartBeat()
{
}

void HeartBeat::setUserid(int user_id)
{
	std::lock_guard<std::recursive_mutex> lock(m_mutex);

}
