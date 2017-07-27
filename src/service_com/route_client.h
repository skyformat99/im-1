#ifndef _ROUTE_CLIENT_H
#define _ROUTE_CLIENT_H

#include "tcp_client.h"
#include <YouMai.Basic.pb.h>
#include "typedef.h"
#include <tcp_server.h>
using namespace com::proto::basic;

class RouteClient:public TcpClient {
public:
    RouteClient();
	void SetRegistInfo(std::string _ip, int _port,int type,TcpServer* s);
	void SetRegistInfo(std::string _ip, int _port);
    int RouteTarget(PDUBase &_base, UserId_t _userid, const std::string &_sessionid, COMMANDID _session_type, bool is_offline_storage);

    virtual void OnRecv(PDUBase* _base);
    virtual void OnConnect();
    virtual void OnDisconnect();
private:
	std::string regist_ip_;
	int regist_port_;

	void RegistService(std::string _ip, int _port);
};

#endif
