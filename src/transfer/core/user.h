#ifndef _USER_H_
#define _USER_H_
#include <string>
#include <list>
#include <mutex>
struct MsgInfo {
	std::string id;
	int expire;
};
class User {
public:
	User();
	User(int id, int nid, int status);
	int SetUserInfo(const int nid, int status);
	int GetNid();
	void Push(std::string& msg_id,int expire);
	int GetOfflineMsg(std::list<std::string>& msg_list);
	int CheckCache();
public:
	int            m_id;
	int            m_nid;
	short          m_status;
	std::list<MsgInfo*> m_msg_list;
	std::recursive_mutex m_msg_list_mutex;
};

#endif
