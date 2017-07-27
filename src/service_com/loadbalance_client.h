#ifndef _LOADBALANCE__CLIENT_H
#define _LOADBALANCE__CLIENT_H


#include "tcp_client.h"
#include "YouMai.Basic.pb.h"
#include "typedef.h"

using namespace com::proto::basic;

class LoadBalanceClient :public TcpClient {
public:
	LoadBalanceClient();
	void SetRegistInfo(std::string _ip, int _port);
	void ReportOnliners(int num);
	virtual void OnRecv(PDUBase* _base);
	virtual void OnConnect();
	virtual void OnDisconnect();

private:
	std::string regist_ip_;
    int regist_port_;
	void RegistService(std::string _ip, int _port);

};


#endif
