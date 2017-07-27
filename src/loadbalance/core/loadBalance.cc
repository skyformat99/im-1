#include "loadBalance.h"

#include "YouMai.User.pb.h"
#include "YouMai.Loadbalance.pb.h"
#include "YouMai.Route.pb.h"
#include "log_util.h"
#include "logic_util.h"
#include <unistd.h>
#include "time_util.h"
#include <log_util.h>
using namespace com::proto::basic;
using namespace com::proto::user;
using namespace com::proto::loadbalance;
using namespace com::proto::route;


LoadBalanceServer::LoadBalanceServer() {
	index_ = 0;
}


void LoadBalanceServer::OnDisconn(int _sockfd) {
	bool is_need_set_onlinestatus = false;

	if (delete_loadbalance_from_list(_sockfd)) {
		LOGE("通信服务器断开连接，sockfd:%d", _sockfd);
		is_need_set_onlinestatus = true;
	}
	CloseFd(_sockfd);
}

static unsigned int dictGenHashFunction(const unsigned char *buf, int len) {
        unsigned int hash = 5381;
        while (len--)
             hash = ((hash << 5) + hash) + (*buf++); /* hash * 33 + c */
        return hash;
}
/*
* 分配负载
*/
void LoadBalanceServer::AllocateLoadbalance(std::string &_ip, short &_port, UserId_t _userid) {
	std::lock_guard<std::recursive_mutex> lock_1(loadbalance_mutex_);
	if (loadbalance_list_.size() == 0) {
		LOGE("负载均衡服务器数量为0");
		return;
	}
	//对当前最大索引取模，找到不小于LoadbalanceObject id的 object
	int mod = (dictGenHashFunction((const unsigned char*)&_userid,sizeof(int)))%index_;
	
	Loadbalance_List_t::iterator it = loadbalance_list_.begin();
	Loadbalance_List_t::iterator suit = it;
	
	while (it != loadbalance_list_.end()) {
		if (it->id_ >= mod) {
			if (it->state == ROUTE_STATE_ERR) {
				loadbalance_list_.erase(it++);
				continue;
			}
			suit = it;
			break;
		}
		it++;
	}
	if (it == loadbalance_list_.end()) {
		suit = loadbalance_list_.begin();
	}

	it = loadbalance_list_.begin();
	Loadbalance_List_t::iterator less = it;
	while (it != loadbalance_list_.end()) {
		if (less->current_balance_user_num_ >= it->current_balance_user_num_) {
			less = it;
		}
		++it;
	}
	if (suit->current_balance_user_num_ > less->current_balance_user_num_ + 50) {
		suit = less;
	}
		
	_ip = suit->ip_;
	_port = suit->port_;
	LOGD("loadbalance requst ,choose server%s, %d", _ip.c_str(), _port);
}

void LoadBalanceServer::OnRecv(int _sockfd, PDUBase &_base) {
	//printf("recv cmd:%d\n",_base.command_id);
	
}

void LoadBalanceServer::OnRecv(int _sockfd, PDUBase* _base) {
	//printf("recv cmd:%d\n",_base.command_id);
	switch (_base->command_id) {
	case REGIST_COMMUNICATIONSERVICE:
		ProcessRegistService(_sockfd, *_base);
		break;
	case REPORT_ONLINERS:
		ProcessReportOnliners(_sockfd, *_base);
	default:
		break;
	}
	delete _base;
}


void LoadBalanceServer::OnConn(int _sockfd)
{

}

void LoadBalanceServer::OnSendFailed(PDUBase &_data) {
	/// TODO:发送失败情况的处理
}

void LoadBalanceServer::ProcessRegistService(int _sockfd, PDUBase &_pack) {
	Regist_CommunicationService regist;

	if (!regist.ParseFromArray(_pack.body.get(), _pack.length)) {
		LOGE("ProcessRegistService包解析错误 cmd(%d) seq_id(%d)", _pack.command_id, _pack.seq_id);
		return;
	}
	
	std::lock_guard<std::recursive_mutex> lock_1(loadbalance_mutex_);
	int start = 0;
	LoadbalanceObject obj;
	Loadbalance_List_t::iterator it = loadbalance_list_.begin();
	while (it != loadbalance_list_.end()) {
		if (start < it->id_) {
			break;
		}
		else {
			start = it->id_ + 1;
		}
		it++;
	}

	obj.id_ = start;
	obj.ip_ = regist.ip();
	obj.port_ = regist.port();
	obj.sockfd_ = _sockfd;
	obj.regist_timestamp_ = TimeUtil::timestamp_int();
	if (it == loadbalance_list_.end()) {
		loadbalance_list_.push_back(obj);
		index_ = start+1;
	}
	else {
		loadbalance_list_.insert(it, obj);
	}
	LOGD( "connection server rigst(index:%d) ip:%s, port:%d", start, regist.ip().c_str(), regist.port());
}

void LoadBalanceServer::ProcessReportOnliners(int _sockfd, PDUBase & _pack)
{
	Report_onliners report;
	if (!report.ParseFromArray(_pack.body.get(), _pack.length)) {
		LOGE("ProcessReportOnliners包解析错误 cmd(%d) seq_id(%d)", _pack.command_id, _pack.seq_id);
		return;
	}
	std::lock_guard<std::recursive_mutex> lock_1(loadbalance_mutex_);
	int start = 0;
	LoadbalanceObject obj;
	Loadbalance_List_t::iterator it = loadbalance_list_.begin();
	while (it != loadbalance_list_.end()) {
		if (it->sockfd_ == _sockfd) {
			it->current_balance_user_num_ = report.onliners();
			LOGD("ip:%s,port:%d onliners:%d", it->ip_.c_str(), it->port_, it->current_balance_user_num_);
			break;
		}
		it++;
	}
}


int LoadBalanceServer::SendProto(int _sockfd, google::protobuf::Message &_msg, int _command_id, int _seq_id, int _userid) {
	PDUBase base;
	std::shared_ptr<char> body(new char[_msg.ByteSize()]);

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


bool LoadBalanceServer::find_loadbalance_from_list(int _sockfd, std::string &_ip, int &_port) {
	std::lock_guard<std::recursive_mutex> lock_1(loadbalance_mutex_);
	LoadbalanceObject object;
	object.sockfd_ = _sockfd;
	Loadbalance_List_t::iterator it;
	it = std::find(loadbalance_list_.begin(), loadbalance_list_.end(), object);
	if (it != loadbalance_list_.end()) {
		_ip = it->ip_;
		_port = it->port_;
		return true;
	}
	return false;
}

bool LoadBalanceServer::delete_loadbalance_from_list(int _sockfd) {
	std::lock_guard<std::recursive_mutex> lock_1(loadbalance_mutex_);
	LoadbalanceObject object;
	object.sockfd_ = _sockfd;
	Loadbalance_List_t::iterator it;
	it = std::find(loadbalance_list_.begin(), loadbalance_list_.end(), object);
	if (it != loadbalance_list_.end()) {
		loadbalance_list_.erase(it);
	//	it->state = ROUTE_STATE_ERR;
		return true;
	}
	return false;
}

bool LoadBalanceServer::update_loadbalance_from_list(int _sockfd) {
	std::lock_guard<std::recursive_mutex> lock_1(loadbalance_mutex_);
	Loadbalance_List_t::iterator it = loadbalance_list_.begin();
	while (it != loadbalance_list_.end()) {
		if (it->sockfd_ == _sockfd) {
			it->current_balance_user_num_++;
		}
		it++;
	}
	return false;
}

LoadbalanceObject* LoadBalanceServer::get_loadbalance_less()
{
	std::lock_guard<std::recursive_mutex> lock_1(loadbalance_mutex_);
	Loadbalance_List_t::iterator it = loadbalance_list_.begin();
	Loadbalance_List_t::iterator less = it;
	while (it != loadbalance_list_.end()) {
		if (it->state == ROUTE_STATE_ERR) {
			loadbalance_list_.erase(it++);
			continue;
		}
		LOGD("less:num:%d,cur num:%d", less->current_balance_user_num_, it->current_balance_user_num_);
		if (less->current_balance_user_num_ >= it->current_balance_user_num_) {
			less = it;
		}
		++it;
	}
	return &(*less);
}

void LoadBalanceServer::ServiceInfo() {
	
}

LoadbalanceObject::LoadbalanceObject()
{
	current_balance_user_num_ = 0;
	state = ROUTE_STATE_OK;
}
