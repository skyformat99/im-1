#include "route_client.h"

#include "YouMai.Route.pb.h"
#include "log_util.h"
#include <transfer.h>

using namespace com::proto::basic;
using namespace com::proto::route;

RouteClient::RouteClient() {

}

void RouteClient::SetRegistInfo(std::string _ip, int _port)
{
	regist_ip_ = _ip;
	regist_port_ = _port;
}


void RouteClient::OnRecv(PDUBase* _base) {
	m_server->OnRoute(_base);
}

void RouteClient::init(Transfer* server){
    m_server=server;
}
void RouteClient::OnConnect() {
	LOGD("service transfer has established connection with route server");
	RegistService(regist_ip_, regist_port_);
}

void RouteClient::OnDisconnect() {

}

void RouteClient::RegistService(std::string _ip, int _port)
{
	Regist_CommunicationService regist;
	regist.set_ip(_ip);
	regist.set_port(_port);
	regist.set_service_type(TRANSFER_SERVER);
	SendProto(regist, REGIST_COMMUNICATIONSERVICE, 0);
}
