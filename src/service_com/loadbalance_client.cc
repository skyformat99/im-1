#include "loadbalance_client.h"

#include "main.h"
#include "YouMai.Loadbalance.pb.h"
#include "YouMai.Route.pb.h"
#include "log_util.h"

using namespace com::proto::loadbalance;
using namespace com::proto::basic;
using namespace com::proto::route;

LoadBalanceClient::LoadBalanceClient() {

}

void LoadBalanceClient::SetRegistInfo(std::string _ip, int _port) {
	regist_ip_ = _ip;
	regist_port_ = _port;
}

void LoadBalanceClient::ReportOnliners(int num)
{
	Report_onliners req;
	req.set_onliners(num); 
	SendProto(req, REPORT_ONLINERS, 0);

}

void LoadBalanceClient::RegistService(std::string _ip, int _port) {
	Regist_CommunicationService regist;
	regist.set_ip(_ip);
	regist.set_port(_port);
	regist.set_service_type(COM_SERVER);
	SendProto(regist, REGIST_COMMUNICATIONSERVICE, 0);
}

void LoadBalanceClient::OnRecv(PDUBase* _base) {
	
}

void LoadBalanceClient::OnConnect() {
	LOGT("service com has established connection with loadbalance server");
	RegistService(regist_ip_, regist_port_);
	
}

void LoadBalanceClient::OnDisconnect() {

}

