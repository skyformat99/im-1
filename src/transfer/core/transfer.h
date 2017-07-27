#ifndef __TRANSFER_H_
#define __TRANSFER_H_
#include <tcp_server.h>
#include <route_client.h>
#include <redis_client.h>
#include <list>
#include <mutex>
#include <threadpool/ThreadPool.h>
#include <user.h>
#include <map>
#include "YouMai.Transfer.pb.h"
using namespace com::proto::transfer;

#define REMOVE_MSG_CACHE_PER_TIME                  10000
#define CHECK_USER_OFFLINE_MSG_PER_TIME            100000

class Transfer;
enum Comstate {
	STATE_NONE,
	STATE_CONNECT,
	STATE_CONNECTING,
	STATE_CONNECTED
}; 

struct Node { 
	std::string    ip;
	short          port;
	int            id;
	int            state;
	int            type;
	int            fd;
};

struct BroadcastMsgInfo {
	int                      expire;
	TransferBroadcastNotify  broadcast;
	PDUBase*                 pdu;
};
class Transfer :public TcpServer {
public:
	Transfer();
	~Transfer();

	void SyncUserData(std::list<std::string>& _keys);

	void InitIMUserList();

	int init();
	int start();
	virtual void OnRecv(int _sockfd, PDUBase* _base);
	virtual void OnConn(int _sockfd);
	virtual void OnDisconn(int _sockfd);
	virtual void OnSendFailed(PDUBase &_data);
	void ProcessBusiMsg(int _sockfd,PDUBase*  _base);
	void ProcessBroadcast(int _sockfd, PDUBase&  _base);
	
	//process route msg
	void OnRoute(PDUBase* _base);
	void ProcessRouteMsg(PDUBase* _base);
	void ProcessRouteBroadcast(PDUBase& _base);
	void ProcessUserState(PDUBase& _base);

	//node info
	void AddNode(std::string& ip, short port, std::string& id);
	Node* FindNode(std::string& id);
	
	int GetSockByNodeID(int node_id);

	//user
	User* FindUser(int user_id);
	User* AddUser(int user_id,int nid, int status);
	void HandlerOfflineMsg(User* user);

	

private:
	//timer
	static void Timer(int fd, short mask, void *clientData);
	void Cron();
	//remove expire msg
	void CheckCache();

	//count
	void count();
  

private:
	RouteClient         m_route_client;
	ThreadPool*         m_thread_pool;


	//node
	std::list<Node*>      m_node_list;

	std::recursive_mutex  m_node_list_mutex;

	std::map<int, User*>   m_user_map;
	std::recursive_mutex   m_user_map_mutex; 

	//broadcast msg
	std::map<std::string, BroadcastMsgInfo*>    m_msg_map;
	std::recursive_mutex                       m_msg_map_mutex;

	std::string                  listen_ip;
	short                        listen_port;
	
	RedisClient                  redis_client;
};
#endif
