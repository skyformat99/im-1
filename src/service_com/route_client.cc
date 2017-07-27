#include "route_client.h"
#include "YouMai.Route.pb.h"
#include "log_util.h"
#include<connection_server.h>

using namespace com::proto::basic;
using namespace com::proto::route;

extern ConnectionServer connect_server;
RouteClient::RouteClient() {

}

void RouteClient::SetRegistInfo(std::string _ip, int _port)
{
	regist_ip_ = _ip;
	regist_port_ = _port;
}

int RouteClient::RouteTarget(PDUBase &_base, UserId_t _userid, const std::string &_sessionid, COMMANDID _session_type, bool is_offline_storage) {
    Route_Req route_req;
    route_req.add_targets(_userid);
    route_req.set_commandid(_base.command_id);
    route_req.set_sessionid(_sessionid);
    route_req.set_session_type(_session_type);
    route_req.set_is_offline_storage(is_offline_storage);

    std::shared_ptr<char> sp_buf;
    int length = OnPduPack(_base, sp_buf);
    route_req.set_route_obj(sp_buf.get(), length);

    return SendProto(route_req, ROUTE_REQ, 0);
}

void RouteClient::OnRecv(PDUBase* _base) {
	connect_server.OnRoute(_base);
}

void RouteClient::OnConnect() {
    LOGT("service com has established connection with route server");
	RegistService(regist_ip_, regist_port_);
}

void RouteClient::OnDisconnect() {

}

void RouteClient::RegistService(std::string _ip, int _port)
{
	Regist_CommunicationService regist;
	regist.set_ip(_ip);
	regist.set_port(_port);
	regist.set_service_type(COM_SERVER);
	SendProto(regist, REGIST_COMMUNICATIONSERVICE, 0);
}
