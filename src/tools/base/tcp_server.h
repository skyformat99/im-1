#ifndef _TCP_SERVER_H
#define _TCP_SERVER_H

#include "pdu_base.h"
#include "../pdu_util.h"
#include "connection.h"
#include <unordered_map>
#include <mutex>
#include <map>
#include <sdEventloop.h>
#include <deleter.h>
#define TCP_MAX_BUFF 51200
#define MAX_EVENTS 100
#define TIMEOUT_INTERVAL 100

#define CONFIG_FDSET_INCR  128


#define run_with_period(_ms_) if (!(m_cronloops%((_ms_)/1000)))

class TcpServer:public PduUtil {
public:
	
	TcpServer();
    int getConnectNum();
    virtual ~TcpServer();
	virtual void OnRecv(int _sockfd, PDUBase* _base) = 0;
    virtual void OnConn(int _sockfd) = 0;
    virtual void OnDisconn(int _sockfd) = 0;
    virtual void OnSendFailed(PDUBase &_data) = 0;

    virtual void StartServer(std::string _ip, short _port);
	
	int StartClient(std::string _ip, short _port);
	long long  CreateTimer(long long milliseconds, sdTimeProc * proc, void * clientData);
    void delEvent(int fd,int mask);


public:
    int epollfd_;
    int listen_sockfd_;


protected:
    std::string ip_; // 服务端监听IP
    int port_; // 服务端监听端口
    int listen_num;	// 监听的SOCKET数量

    std::mutex send_mutex;

    void PollStart();
    void CloseFd(int _sockfd);
	void Accept(int _sockfd);
    bool Send(int _sockfd, PDUBase &_data);
    bool Send(int _sockfd, const char *buffer, int length);

	bool Send(int _sockfd,  msg_t _msg, int _length);

	static void write_cb(int fd, short mask, void* privdata);

	//  void accept_cb(int _sockfd, char *_ip, short _port);
    void close_cb(int _sockfd);
    int read_cb(int _sockfd, struct epoll_event &_ev);

	void parse(Connection * conn);

    void setlinger(int _sockfd);
    void setreuse(int _sockfd);
    void setnodelay(int _sockfd);
	int anetKeepAlive(int fd, int interval);
	void setnonblocking(int _sockfd);
    void bindsocket(int _sockfd, const char *_pAddr, int _port);
    void listensocket(int _sockfd, int _conn_num);

private:
	static  void accept_cb(int fd, short mask,void* privdata);
	static void read_cb(int fd, short mask, void* privdata);
private:
	SdEventLoop*  loop;
	std::map<int, Connection*> m_conns;
	std::recursive_mutex       m_ev_mutex;
};

#endif
