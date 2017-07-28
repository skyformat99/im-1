#ifndef _CONNECTION_SERVER_H
#define _CONNECTION_SERVER_H

#include "route_client.h"
#include "loadbalance_client.h"
#include "client_object.h"
#include "YouMai.User.pb.h"
#include "YouMai.Route.pb.h"
#include "tcp_server.h"
#include "block_tcp_client.h"
#include <google/protobuf/message.h>
#include <list>
#include <string>
#include <pthread.h>
#include <atomic>
#include <ackMsgMap.h>
#include <Lock.h>
#include <ThreadPool.h>
#define NEW_VERSION                   1
#define MSG_ACK_TIME                  5


using namespace com::proto::basic;
using namespace com::proto::user;
using namespace com::proto::route;

typedef std::unordered_map<Socketfd_t, UserId_t> Socket_Userid_Map_t;
typedef std::unordered_map<UserId_t, ClientObject> User_Connect_Map_t;
typedef std::unordered_map<UserId_t, std::list<PDUBase> > Send_Queue_t;
typedef std::unordered_map<std::string, UserId_t > Phone_userid_t;

class Ackmsg {
public:
	Ackmsg(uint64_t msg_id_, PDUBase& pdu_):msg_id(msg_id_), pdu(pdu_){}
	uint64_t    msg_id;
	PDUBase      pdu;
};
class ConnectionServer:public TcpServer {
public:
    ConnectionServer();
	int init();
	void StartServer(std::string _ip, short _port);

	virtual void OnRecv(int _sockfd, PDUBase* _base);
    virtual void OnConn(int _sockfd);
    virtual void OnDisconn(int _sockfd);
	virtual void OnSendFailed(PDUBase &_data);

	int PreProcessPack(int _sockfd, int _userid, PDUBase &_base);
	void ProcessClientMsg(int _sockfd, PDUBase*  _base);
	
	
	void ProcessRegistRsp(PDUBase & _pack);

	void BroadUserState(int user_id, UserState state);

   

    void OnRoute(PDUBase* _base);
	void ProcessRouteMsg(PDUBase* _base);
	void ProcessUserStatSyncRsp(PDUBase & _pack);
	
	void ProcessUserStatSyncRsp(RouteSyncUserStateRsp & rsp);
	void ProcessUserStatSyncReq(PDUBase & _pack);
    void ProcessIMChat_fromRoute(PDUBase &_base);

	static void Timer(int fd, short mask, void* privdata);
public:
    void ConsumeHistoryMessage(UserId_t _userid);
    void ResetPackBody(PDUBase &_pack, google::protobuf::Message &_msg, int _commandid);
    void RegistUserToRoute(ClientObject &_client);
    void RegistUsersToRoute();
    void ResendFailedPack(int _sockfd, UserId_t _userid);
    void ReplyChatResult(int _sockfd, PDUBase &_pack, ERRNO_CODE _code, bool is_target_online = false,uint64_t msg_id=0);
 
    void ProcessHeartBeat(int _sockfd, PDUBase _pack);
    void ProcessUserLogin(int _sockfd, PDUBase&  _base);
	void ProcessUserLogout(int _sockfd, PDUBase&  _base);
    void ProcessIMChat_Personal(int _sockfd, PDUBase& _base);

	void ProcessChatMsg_ack(int _sockfd, PDUBase & _base);

	void ProcessIMChat_broadcast(int _sockfd, PDUBase& _base);
	void ProcessBulletin_broadcast(int _sockfd, PDUBase& _base);

	void SaveOfflineMsg_(int _userid, char * data, int len);

	void SaveOfflineMsg(int _userid, PDUBase& _base);
	

    int BuildUserCacheInfo(int _sockfd, User_Login& _login,int version);
    int PhoneQueryUserId(std::string _phone, int& _userid);
    int UserIdQueryPhone(int _userid, std::string &_phone);

    bool IsUserOnline(UserId_t _userid);
    void LoginUtil(Socketfd_t _sockfd, UserId_t _userid);
    void ServiceInfo();
	BlockTcpClient* getBlockTcpClient();

	int getOnliners();
	void reportOnliners();
private:

    bool find_userid_by_sockfd(int _sockfd, int &_userid, bool is_del = false);
    bool find_client_by_userid(int _userid, ClientObject &_client, bool is_set_offline = false);
	void set_user_state(int _userid, OnlineStatus state);
	int get_user_state(int _userid);
	uint64_t getMsgId();

	bool need_send_msg(int _userid, int _sockfd, PDUBase & _base, uint64_t msg_id);
	
	
	void record_waiting_ackmsg(int _userid, uint64_t msg_id);
	
	void delete_ack_msg(int _userid, uint64_t msg_id);
    static void check_msg(ConnectionServer* s);
    void check_send_msg();
private:
    // 本服务器的ip和端口
    std::string ip_;
    int port_;

    // 路由服务器的ip和端口
    std::string route_ip_;
    int route_port_;

	

	//负载均衡服务器
	std::string loadbalance_ip_;
	int loadbalance_port_;

	std::atomic_int    onliners_;
private:
	int        ack_time_;
    RouteClient route_client_;
	LoadBalanceClient  loadbalance_client_;
    User_Connect_Map_t user_map_;
    std::recursive_mutex user_map_mutex_;

    Send_Queue_t send_failed_cache_queue_;
    std::recursive_mutex send_failed_cache_queue_mutex_;

    Socket_Userid_Map_t socket_userid_;
    std::recursive_mutex socket_userid_mutex_;

	//map phone --userid
	Phone_userid_t  phone_userid_;
	std::recursive_mutex phone_userid_mutex_;
	std::map<pthread_t, BlockTcpClient*> m_blockTcpClient;
//    BlockTcpClient block_tcp_client_;
    std::recursive_mutex block_tcp_client_mutex_;


	//send msg
	std::map<int, std::list<Ackmsg*>>  m_send_msg_map_;
	//std::recursive_mutex               m_send_msg_mutex_;
	CRWLock                            m_rwlock_;

	//wait ack userid
	AckMsgMap<int, int>         m_ack_msg_map_;
	std::recursive_mutex        m_ack_msg_mutex_;

	int                         m_node_id_;
	std::recursive_mutex        m_msgid_mutex_;
	int                         m_cur_index_;
	time_t                      m_lastTime;

	//stastic
	atomic_ullong               m_total_recv_pkt;

	ThreadPool*                 m_offline_works ;
	
};

#endif
