#ifndef _ROUTE_SERVER_H
#define _ROUTE_SERVER_H

#include <list>
#include <mutex>
#include <algorithm>
#include "typedef.h"
#include "route_object.h"
#include "loadbalance_object.h"
#include "pdu_base.h"
#include "tcp_server.h"
#include <google/protobuf/message.h>

class Client;
typedef std::list<Client> Client_list_t;
typedef std::unordered_map<UserId_t, RouteObject*> User_Route_Map_t;
typedef std::unordered_map<UserId_t, std::string> User_Phone_Map_t;

struct Client {
	Client() :state(0), sockfd(-1) {
	}
	std::string name;
	std::string ip;
	short    port;
	int      sockfd;
	int      type;
	int      id;
	int      state;  //0 :not connect 1:connect

};


class RouteServer:public TcpServer {
public:
    RouteServer();
	int init();
	int start();
	void InitIMUserList();
	void SyncUserData(std::list<std::string>& _keys);

	void ReSyncUserData();

	virtual void OnRecv(int _sockfd, PDUBase* _base);
	void OnConn(int _sockfd);
    virtual void OnDisconn(int _sockfd);
    virtual void OnSendFailed(PDUBase &_data);
	void ProcessCommonMsg(int _sockfd, PDUBase* _pack);
	void ProcessUserLogin(int sockfd, PDUBase& _pack);
	void ProcessUserLogOff(PDUBase &_pack);
	void BroadcastUserState(int user_id, UserState state);
	void ProcessRegistService(int _sockfd, PDUBase & _pack);
	void BroadcastSerivce(Client& client);
	

	void ProcessUserStatSyncRsp(int _sockfd, PDUBase & _base);
 
    void ProcessRouteMessage(PDUBase &_pack);
    void ProcessOnlineCheck(int _sockfd, PDUBase &_pack);
    void ProcessPhoneCheck(int _sockfd, PDUBase &_pack);

	
    int SendProto(int _sockfd, google::protobuf::Message &_msg, int _command_id, int _seq_id, int _userid);

    void service_info();

	static void timer(int fd, short mask, void * privdata);

	int count();

private:
    bool find_phone_by_userid(int _userid, std::string &_phone);
	RouteObject * find_route_by_userid(int _userid);
	void set_user_state(int _userid, int state);
	void set_user_route_state(int _userid, int _sockfd, int state);
	int get_user_nid(int _userid);
	int get_client_nid(int _sockfd);

	//node manager
	
	Client*  get_client(const std::string& ip, short port);

  
public:
	std::string     ip_;
	short           port_;
	Client_list_t   client_list_;
	std::recursive_mutex client_list_mutex_;

    User_Route_Map_t user_map_;
    std::recursive_mutex user_map_mutex_;

};


#endif
