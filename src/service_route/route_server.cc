#include "route_server.h"
#include "YouMai.User.pb.h"
#include "YouMai.Transfer.pb.h"
#include "YouMai.Route.pb.h"
#include "log_util.h"
#include "time_util.h"
#include "main.h"
#include "logic_util.h"
#include <unistd.h>
#include <algorithm>
#include "config_file_reader.h"
using namespace com::proto::basic;
using namespace com::proto::user;
using namespace com::proto::transfer;
using namespace com::proto::route;
extern RouteServer route_server;

static int parse(const std::string& src, std::string* value,int num) {
    LOGD("parse src:%s",src.c_str());
	int n = 0;
	auto begin = src.begin();
	if (src != "") {
		for (auto it = src.begin(); it != src.end(); it++) {
			if (*it != ' ' ) {
				continue;
			}
			value[n++] = std::string(begin, it);
			if (n > num) {
				LOGE("too manay parameter num");
				return -1;
			}
			while (*(it++)) {
				//trim space case
				if (*it == ' ' || *it=='\t') {
					continue;
				}
				break;
			}
			begin = it;

		}
	}
    value[n++]=std::string(begin,src.end());
	if (n < num) {
		LOGE("parameter num(%d) Too few ",n);
		return -1;
	}
	return 0;
}
RouteServer::RouteServer() {
	
}

int RouteServer::init()
{
	ConfigFileReader reader(CONF_ROUTE_URL);
	ip_ = reader.ReadString(CONF_ROUTE_IP);
	port_ = reader.ReadInt(CONF_ROUTE_PORT);
	
	std::string value[4];
	Client client;
	
	auto transfer_list = reader.equal("transfer");
	for (auto it = transfer_list.first; it != transfer_list.second; ++it) {
		if (::parse(it->second, value, 4) == -1) {
			return -1;
		}
		client.name = value[0];
		client.ip = value[1];
		client.port = atoi(value[2].c_str());
		client.id = atoi(value[3].c_str());
		client.type = TRANSFER_SERVER;
		client_list_.push_back(client);
		LOGD("transfer(name:%s ,ip:%s,port:%d,nid:%d)",client.name.c_str(), client.ip.c_str(), client.port, client.id);
	}
	auto com_list = reader.equal("com");
	for (auto it = com_list.first; it != com_list.second; ++it) {
		if (::parse(it->second, value, 4) == -1) {
			return -1;
		}
		client.name = value[0];
		client.ip = value[1];
		client.port = atoi(value[2].c_str());
		client.id = atoi(value[3].c_str());
		client.type = COM_SERVER;
		client_list_.push_back(client);
		LOGD("com(name:%s ,ip:%s,port:%d,nid:%d)", client.name.c_str(), client.ip.c_str(), client.port, client.id);
	}
	return 0;
}

int RouteServer::start()
{
	StartServer(ip_, port_);
}

void RouteServer::SyncUserData(std::list<std::string>& _keys) {
    LOGD("同步redis数据%d", _keys.size());
    if (_keys.size() == 0) return;

    for (auto it = _keys.begin(); it != _keys.end(); it++) {
        std::string phone = LogicUtil::get_phone(*it);
        std::string userid = redis_client.GetUserId(phone);
        if (userid != "") {
			RouteObject*  object=new   RouteObject;
			object->userid_=atoi(userid.c_str());
			object->phone_=phone;
            user_map_[object->userid_] = object;
            //LOGD("正在同步数据库，userid:%s, phone:%s", userid.c_str(), phone.c_str());
        }
    }
}

void RouteServer::ReSyncUserData()
{
	std::list<std::string> keys;

	redis_client.GetIMUserList(keys);
	LOGI("用户数量:%d", keys.size());
	for (auto it = keys.begin(); it != keys.end(); it++) {
		std::string phone = LogicUtil::get_phone(*it);
		std::string userid = redis_client.GetUserId(phone);
		if (userid != "") {
			
			RouteObject*  object = find_route_by_userid(atoi(userid.c_str()));
			if (!object) {
				object = new RouteObject;
				object->userid_ = atoi(userid.c_str());
				object->phone_ = phone;
				std::lock_guard<std::recursive_mutex> lock_1(user_map_mutex_);
				user_map_[object->userid_] = object;
			}
			
			//LOGD("正在同步数据库，userid:%s, phone:%s", userid.c_str(), phone.c_str());
		}
	}
}

void RouteServer::InitIMUserList() {
    std::list<std::string> keys;

    redis_client.GetIMUserList(keys);
    LOGI("用户数量:%d", keys.size());

    struct timeval start, stop;
    gettimeofday(&start, NULL);
	SyncUserData(keys);
    gettimeofday(&stop, NULL);
    int sec = stop.tv_sec - start.tv_sec;
    int usec = stop.tv_sec - start.tv_sec;
    if (usec < 0) {
       usec += 1000;
       sec -= 1;
    }
    LOGD("sync success ,total users:%d %dms",user_map_.size(),sec * 1000 + usec / 1000);
	CreateTimer(300000, timer, this);
}


void RouteServer::OnDisconn(int _sockfd) {
    bool is_need_set_onlinestatus = false;
	std::lock_guard<std::recursive_mutex> lock_1(client_list_mutex_);
    for(auto it=client_list_.begin();it!=client_list_.end();++it){
        if(it->sockfd==_sockfd){
            it->state=0;
            break;
        }
    }
    
	// 将该服务器的所有连接用户全部置为下线状态
	std::lock_guard<std::recursive_mutex> lock_2(user_map_mutex_);
    auto it=user_map_.begin();
    while(it!=user_map_.end()){
        if(it->second->sockfd_ == _sockfd){
			it->second->online_status_= OnlineStatus_Offline;
            it->second->sockfd_=-1;
            ++it;
            continue;
        }
        it++;
    }
	//std::remove_if(user_map_.begin(), user_map_.end(), [_sockfd]( std::pair< UserId_t, RouteObject>& it) {return it.second.sockfd_ == _sockfd; });
    CloseFd(_sockfd);
}


extern  int total_recv_pkt;
extern int online_check;

void RouteServer::OnRecv(int _sockfd, PDUBase* _base) {
	//printf("recv cmd:%d\n",_base.command_id);
    switch (_base->command_id) {
    case REGIST_COMMUNICATIONSERVICE:
        ProcessRegistService(_sockfd, *_base);
        break;
   
    case USER_LOGOFF:
        ProcessUserLogOff(*_base);
        break;
    case ROUTE_ONLINECHECK:
        ++online_check;
        ProcessOnlineCheck(_sockfd, *_base);
        break;
    case ROUTE_PHONE_CHECK:
        ProcessPhoneCheck(_sockfd, *_base);
        break;
	case USER_LOGIN:
		ProcessUserLogin(_sockfd, *_base);
		break;
    default:
		thread_pool.enqueue(&RouteServer::ProcessCommonMsg, this, _sockfd, _base);
		return;
    }
	delete _base;
}


void RouteServer::OnConn(int _sockfd)
{

}

void RouteServer::OnSendFailed(PDUBase &_data) {
    /// TODO:发送失败情况的处理
}

void RouteServer::ProcessCommonMsg(int _sockfd, PDUBase * _pack)
{
	switch (_pack->command_id) {
	
	case ROUTE_REQ:
		++total_recv_pkt;
		ProcessRouteMessage(*_pack);;
		break;
	case CID_USER_STAT_SYNC_RSP:
	    ProcessUserStatSyncRsp(_sockfd,*_pack);	
        break;
    default:
        LOGE("unkown cmd:%d\n",_pack->command_id);
        break;
	}
	delete _pack;
}

/*
* 路由用户注册
*/
void RouteServer::ProcessUserLogin(int _sockfd, PDUBase& _pack) {
	User_Login login;
	if (!login.ParseFromArray(_pack.body.get(), _pack.length)) {
		LOGERROR(_pack.command_id, _pack.seq_id, "User_Login包解析错误");
	}

	RouteObject* object;
	if ((object = find_route_by_userid(login.user_id())) == NULL) {
		object = new RouteObject;
	}

	{
		std::lock_guard<std::recursive_mutex> lock_1(user_map_mutex_);
		object->sockfd_ = _sockfd;
		object->userid_ = login.user_id();
		object->phone_ = login.phone();
		object->device_id_ = login.device_id();
		object->device_type_ = login.device_type();
		object->online_status_ = OnlineStatus_Connect;
		user_map_[object->userid_] = object;
	}


	LOGD("user phone:%s, userid:%d login ", login.phone().c_str(), login.user_id());
	BroadcastUserState(login.user_id(), USER_STATE_ONLINE);
}

void RouteServer::ProcessRegistService(int _sockfd, PDUBase &_pack) {
	Regist_CommunicationService regist;

	if (!regist.ParseFromArray(_pack.body.get(), _pack.length)) {
		LOGE("ProcessRegistService包解析错误 cmd(%d) seq_id(%d)", _pack.command_id, _pack.seq_id);
		return;
	}
	RegistRsp rsp;
	rsp.set_result(1);
	
	Client* client = get_client(regist.ip(), regist.port());
	if (!client) {
		rsp.set_result(0);
		return;
	}

	client->sockfd = _sockfd;
	client->state = 1;
	rsp.set_node_id(client->id);
	SendProto(_sockfd, rsp, REGIST_RSP, 0, 0);

	BroadcastSerivce(*client);
	RouteSyncUserStateReq req;
    req.set_node_id(client->id);
	if (client->type == COM_SERVER) {
		SendProto(_sockfd, req, CID_USER_STAT_SYNC_REQ, _pack.seq_id, _pack.terminal_token);
	}
	else if (client->type == TRANSFER_SERVER) {
		RouteUsersStateBroadcast  user_state;
		auto p_user_list = user_state.mutable_user_state_list();
		std::lock_guard<std::recursive_mutex> lock_1(user_map_mutex_);
		int num = 0;
		for (auto it = user_map_.begin(); it != user_map_.end(); it++) {
			if (it->second->online_status_ == OnlineStatus_Connect) {
				++num;
				auto state = p_user_list->Add();
				state->set_user_id(it->first);
				state->set_state(USER_STATE_ONLINE);
				state->set_node_id(get_user_nid(it->first));
				if (!(num % 2000)) {
					SendProto(_sockfd, user_state, CID_USER_STAT_PUSH_REQ, 0, 0);
					p_user_list->Clear();
				}
			}
		}
		if (num % 2000) {
			SendProto(_sockfd, user_state, CID_USER_STAT_PUSH_REQ, 0, 0);
		}
	}
}

void RouteServer::BroadcastSerivce(Client& client)
{
	int _sockfd = client.sockfd;
	

  
	RouteBroadcast broadcast;

	::google::protobuf::RepeatedPtrField< ::com::proto::route::ServiceInfo >* service_list = broadcast.mutable_service_list();
	std::lock_guard<std::recursive_mutex> lock_1(client_list_mutex_);
	for (auto it = client_list_.begin(); it != client_list_.end(); it++) {
		Client& obj = *it;
        if(obj.state==0){
            continue;
        }
        if(obj.sockfd==_sockfd){
            continue;
        }
		ServiceInfo info;
		info.set_id(obj.id);
		info.set_ip(obj.ip);
		info.set_port(obj.port);
		info.set_service_type((ServiceType)obj.type);
        auto mit=service_list->Add();
        mit->CopyFrom(info);
        LOGD("(self)broadcast service type(ip:%s,port:%d) to service(ip:%s,port:%d)",obj.ip.c_str(),obj.port,client.ip.c_str(),client.port);
	}
    PDUBase pack;
    std::shared_ptr<char> body(new char[broadcast.ByteSize()], carray_deleter);
    broadcast.SerializeToArray(body.get(), broadcast.ByteSize());
    pack.body = body;
    pack.command_id = ROUTE_BRAODCAST;
    pack.length = broadcast.ByteSize();
    pack.seq_id = 0;
    Send(_sockfd,pack);
	for (auto it = client_list_.begin(); it != client_list_.end(); it++) {
		Client& obj = *it;
        if(obj.state==0){
            continue;
        }
        if(obj.sockfd==_sockfd){
            continue;
        }
		ServiceInfo info;
		info.set_id(client.id);
		info.set_ip(client.ip);
		info.set_port(client.port);
		info.set_service_type((ServiceType)client.type);
        auto mit=service_list->Add();
        mit->CopyFrom(info);
        LOGD("(other)broadcast service type(ip:%s,port:%d) to service(ip:%s,port:%d)",client.ip.c_str(),client.port,obj.ip.c_str(),obj.port);
    PDUBase pack;
    std::shared_ptr<char> body(new char[broadcast.ByteSize()], carray_deleter);
    broadcast.SerializeToArray(body.get(), broadcast.ByteSize());
    pack.body = body;
    pack.command_id = ROUTE_BRAODCAST;
    pack.length = broadcast.ByteSize();
    pack.seq_id = 0;
    Send(obj.sockfd,pack);
	}
}

void RouteServer::ProcessUserStatSyncRsp(int _sockfd,PDUBase &_base) {
    LOGD("user stat sync rsp");
	RouteSyncUserStateRsp  rsp;
	if (!rsp.ParseFromArray(_base.body.get(), _base.length)) {
		LOGERROR(_base.command_id, _base.seq_id, "SyncUserStateRsp parse fail");
		return;
	}
	auto& user_list = rsp.user_state_list();
	for (auto it = user_list.begin(); it != user_list.end(); it++) {
		const RouteUserState& state = *it;
		set_user_route_state(state.user_id(),_sockfd, OnlineStatus_Connect);
	}
	RouteUsersStateBroadcast  user_state;
	auto p_user_list = user_state.mutable_user_state_list();
	p_user_list->CopyFrom(user_list);

    std::lock_guard<std::recursive_mutex> lock_1(client_list_mutex_);
    for (auto it = client_list_.begin(); it != client_list_.end(); it++) {
        if (it->type == TRANSFER_SERVER) {
	        SendProto(it->sockfd, user_state, CID_USER_STAT_PUSH_REQ, 0, 0);
        }
    }
}

/*
 * 路由消息转发
 */
void RouteServer::ProcessRouteMessage(PDUBase &_pack) {
    Route_Req route_req;
    if (!route_req.ParseFromArray(_pack.body.get(), _pack.length)) {
        LOGERROR(_pack.command_id, _pack.seq_id, "Route_Req包解析错误");
        return;
    }

    int targets_size = route_req.targets_size();
    for (int i = 0; i < targets_size; i++) {
        UserId_t targetid = route_req.targets(i);
		RouteObject* route = find_route_by_userid(targetid);
        if (route) {
            if (route->online_status_ == OnlineStatus_Connect) {
                Send(route->sockfd_, route_req.route_obj().data(), route_req.route_obj().size());
            } else {
                LOGE("路由消息转发，用户phone:%s,userid:%d不在线", route->phone_.c_str(), targetid);
            }
        } else {
            LOGE("路由消息转发，用户phone:%s,userid:%d未登录", route->phone_.c_str(), targetid);
        }
    }
}

void RouteServer::ProcessUserLogOff(PDUBase &_pack) {
    User_LogOff log_off;
    if (!log_off.ParseFromArray(_pack.body.get(), _pack.length)) {
        LOGERROR(_pack.command_id, _pack.seq_id, "User_LogOff包解析错误");
        return;
    }
	set_user_state(log_off.user_id(), OnlineStatus_Offline);
	BroadcastUserState(log_off.user_id(), USER_STATE_OFFLINE);
}

void RouteServer::BroadcastUserState(int user_id, UserState state)
{
	RouteUsersStateBroadcast  user_state;
	auto us = user_state.add_user_state_list();
	us->set_user_id(user_id);
	us->set_state(state);
	us->set_node_id(get_user_nid(user_id));
	PDUBase pack;
	std::shared_ptr<char> body(new char[user_state.ByteSize()], carray_deleter);
	user_state.SerializeToArray(body.get(), user_state.ByteSize());
	pack.body = body;
	pack.command_id = CID_USER_STAT_PUSH_REQ;
	pack.length = user_state.ByteSize();
	pack.seq_id = 0;
	pack.terminal_token = user_id;

	std::lock_guard<std::recursive_mutex> lock_1(client_list_mutex_);
	for (auto it = client_list_.begin(); it != client_list_.end(); it++) {
		if (it->type == TRANSFER_SERVER) {
			Send(it->sockfd, pack);
		}
	}
}

void RouteServer::ProcessOnlineCheck(int _sockfd, PDUBase &_pack) {
    Route_OnlineCheck online;
    Route_OnlineCheck_Ack online_ack;

    online_ack.set_is_online(false);
    if (!online.ParseFromArray(_pack.body.get(), _pack.length)) {
        LOGERROR(_pack.command_id, _pack.seq_id, "Route_OnlineCheck包解析错误");
        SendProto(_sockfd, online_ack, ROUTE_ONLINECHECK_ACK, _pack.seq_id, _pack.terminal_token);
        return;
    }

    RouteObject* route;
	if ((route = find_route_by_userid(online.user_id()))) {
		if (route->online_status_ == OnlineStatus_Connect) {
			online_ack.set_is_online(true);
		}
		else {
			LOGWARN(_pack.command_id, _pack.seq_id, "是否在线检测，用户userid:%d未登录", online.user_id());
		}
	}

    SendProto(_sockfd, online_ack, ROUTE_ONLINECHECK_ACK, _pack.seq_id, _pack.terminal_token);
}

void RouteServer::ProcessPhoneCheck(int _sockfd, PDUBase &_pack) {
    Route_PhoneCheck phonecheck;
    Route_PhoneCheck_Ack phonecheck_ack;

    phonecheck_ack.set_phone("");

    if (!phonecheck.ParseFromArray(_pack.body.get(), _pack.length)) {
        LOGERROR(_pack.command_id, _pack.seq_id, "Route_PhoneCheck包解析错误");
        SendProto(_sockfd, phonecheck_ack, ROUTE_PHONE_CHECK_ACK, _pack.seq_id, _pack.terminal_token);
        return;
    }

    std::string phone;
    if (find_phone_by_userid(phonecheck.user_id(), phone)) {
        phonecheck_ack.set_phone(phone);
    } else {
        LOGWARN(_pack.command_id, _pack.seq_id, "手机号检测，用户userid:%d未注册", phonecheck.user_id());
    }
    SendProto(_sockfd, phonecheck_ack, ROUTE_PHONE_CHECK_ACK, _pack.seq_id, _pack.terminal_token);
}

int RouteServer::SendProto(int _sockfd, google::protobuf::Message &_msg, int _command_id, int _seq_id, int _userid) {
    PDUBase base;
    std::shared_ptr<char> body(new char[_msg.ByteSize()], carray_deleter);

    if (_msg.SerializeToArray(body.get(), _msg.ByteSize())) {
        base.body = body;
        base.length = _msg.ByteSize();
        base.terminal_token = _userid;
        base.command_id = _command_id;
        base.seq_id = _seq_id;
        return Send(_sockfd, base);
    }
    return -1;
}

bool RouteServer::find_phone_by_userid(int _userid, std::string &_phone) {
	std::lock_guard<std::recursive_mutex> lock_1(user_map_mutex_);
	auto it = user_map_.find(_userid);
	if (it != user_map_.end()) {
		_phone = it->second->phone_;
		return true;
	}
	return false;
}

RouteObject* RouteServer::find_route_by_userid(int _userid) {
    std::lock_guard<std::recursive_mutex> lock_1(user_map_mutex_);
    auto it = user_map_.find(_userid);
    if (it != user_map_.end()) {
       
        return it->second;
    }
    return NULL;
}

void RouteServer::set_user_state(int _userid,int state) {
	RouteObject* object = find_route_by_userid(_userid);
	if (object) {
		object->online_status_ = (OnlineStatus)state;
	}
}
void RouteServer::set_user_route_state(int _userid, int _sockfd,int state) {
	RouteObject* object = find_route_by_userid(_userid);
	if (object) {
		object->online_status_ = (OnlineStatus)state;
		object->sockfd_ = _sockfd;
	}
}

int RouteServer::get_user_nid(int _userid)
{
	std::lock_guard<std::recursive_mutex> lock_1(user_map_mutex_);
	int nid = 0;
	auto it = user_map_.find(_userid);
	if (it != user_map_.end()) {

		int fd = it->second->sockfd_;
		nid = get_client_nid(fd);
	}
	return nid;
}

int RouteServer::get_client_nid(int _sockfd)
{
	int nid=0;
	std::lock_guard<std::recursive_mutex> lock_1(client_list_mutex_);
	for (auto it = client_list_.begin(); it != client_list_.end(); it++) {
		if (it->sockfd == _sockfd) {
			nid = it->id;
		}
	}
	return nid;
}

Client* RouteServer::get_client(const std::string & ip, short port)
{
	Client* client = NULL;
	std::lock_guard<std::recursive_mutex> lock1(client_list_mutex_);
	for (auto it = client_list_.begin(); it != client_list_.end(); it++) {
		if (it->port == port && it->ip == ip) {
			client = &*it;
		}
	}
	return client;
}

void RouteServer::service_info() {
    int user_phone_size = 0;
    int user_map_size = 0;
    {
        std::lock_guard<std::recursive_mutex> lock_1(user_map_mutex_);
        user_map_size = user_map_.size();
    }
    printf("服务器信息\n");
    printf("IP:%s 端口:%d\n", ip_.c_str(), port_);
    printf("总注册用户数量:%d\n", user_phone_size);
    printf("连接的用户数在线与不在线:%d\n", user_map_size);

}

void RouteServer::timer(int fd,short mask,void* privdata) {
	RouteServer* server = static_cast<RouteServer*>(privdata);
	if (server) {
		server->count();
	}
}
int RouteServer::count()
{
	int onlines = 0;
	std::lock_guard<std::recursive_mutex> lock_1(user_map_mutex_);
	for (auto it = user_map_.begin(); it != user_map_.end(); it++) {
		if ((it->second->online_status_ == OnlineStatus_Connect)) {
			++onlines;
		}
	}
	LOGD("onlines user:%d", onlines);
}


