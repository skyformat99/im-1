#include "user.h"
#include <ctime>
extern int g_time_cache;
User::User()
{
}

User::User(int id, int nid, int status):m_id(id),m_nid(nid),m_status(status)
{
}

int User::SetUserInfo(int nid, int status)
{
	m_nid = nid;
	m_status = status;
}

int User::GetNid()
{
	return m_nid;
}

int User::GetOfflineMsg(std::list<std::string>& msg_list)
{
	std::lock_guard<std::recursive_mutex> lock(m_msg_list_mutex);
	int size = 0;
	time_t t = time(0);
	for (auto it = m_msg_list.begin(); it != m_msg_list.end(); ) {
		MsgInfo* msg = *it;
		if (msg->expire < t) {
			msg_list.push_back(msg->id);
			++size;
		}
		delete msg;
		it = m_msg_list.erase(it);
	}
	return size;
}

int User::CheckCache()
{
	std::lock_guard<std::recursive_mutex> lock(m_msg_list_mutex);
	auto it = m_msg_list.begin();
	while (it != m_msg_list.end()) {
		MsgInfo* msg = *it;
		if (msg->expire >= g_time_cache) {
			delete msg;
			it=m_msg_list.erase(it);
		}
		else {
			++it;
		}
	}
}

void User::Push(std::string & msg_id, int expire)
{
	MsgInfo* msg = new  MsgInfo;
	msg->id = msg_id;
	msg->expire = expire;
	std::lock_guard<std::recursive_mutex> lock(m_msg_list_mutex);
	m_msg_list.push_back(msg);
}
