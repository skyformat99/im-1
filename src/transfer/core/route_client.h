#ifndef _ROUTE_CLIENT_H
#define _ROUTE_CLIENT_H

#include "tcp_client.h"
#include <YouMai.Basic.pb.h>
#include "typedef.h"
using namespace com::proto::basic;

class Transfer;
class RouteClient :public TcpClient {
public:
	RouteClient();

	void SetRegistInfo(std::string _ip, int _port);

    void init(Transfer* server);
	virtual void OnRecv(PDUBase* _base);
	virtual void OnConnect();
	virtual void OnDisconnect();
private:
	std::string regist_ip_;
	int regist_port_;
    Transfer* m_server;

	void RegistService(std::string _ip, int _port);
};

#endif

