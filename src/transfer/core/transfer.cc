#include "transfer.h"
#include <route_client.h>
#include "YouMai.Transfer.pb.h"
#include "YouMai.Route.pb.h"
#include <log_util.h>
#include <config_file_reader.h>
#include <algorithm>
#include "logic_util.h"
using namespace com::proto::basic;

using namespace com::proto::transfer;
using namespace com::proto::route;

typedef ::google::protobuf::RepeatedField< ::google::protobuf::int32 >  google_list_u32;
int g_time_cache;
Transfer::Transfer()
{
}

Transfer::~Transfer()
{
}

void Transfer::SyncUserData(std::list<std::string>& _keys) {
	if (_keys.size() == 0) return;

	for (auto it = _keys.begin(); it != _keys.end(); it++) {
		std::string phone = LogicUtil::get_phone(*it);
		std::string userid = redis_client.GetUserId(phone);
		int user_id = atoi(userid.c_str());
		if (userid != "") {
			User*  user = new   User(user_id, -1, USER_STATE_OFFLINE);
			m_user_map[user_id] = user;
		}
	}
}

void Transfer::InitIMUserList() {
	std::list<std::string> keys;

	redis_client.GetIMUserList(keys);
	LOGI("total users:%d", keys.size());
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
	LOGD("sync success users:%d  %d ms", m_user_map.size(),sec*1000+usec/1000);

}
int Transfer::init()
{
	// 读取route配置信息，进行连接
	ConfigFileReader reader(CONF_PUBLIC_URL);
	listen_ip = reader.ReadString(CONF_TRANSFER_IP);
	listen_port = reader.ReadInt(CONF_TRANSFER_PORT);
	m_route_client.SetRegistInfo(listen_ip, listen_port);
    m_route_client.init(this);
	std::string route_ip_ = reader.ReadString(CONF_ROUTE_IP);
	short route_port_ = reader.ReadInt(CONF_ROUTE_PORT);

	redis_client.Init_Pool(reader.ReadString(CONF_REDIS_IP), reader.ReadInt(CONF_REDIS_PORT),reader.ReadString(CONF_REDIS_AUTH),1);
	redis_client.SetKeysExpire(reader.ReadInt(CONF_REDIS_EXPIRE));
//	InitIMUserList();

	if (m_route_client.Connect(route_ip_.c_str(), route_port_)) {
		LOGE("connect route server fail");
		return -1;
	}
	LOGD("connect route success");
	CreateTimer(1000, Timer, this);
	m_thread_pool = new ThreadPool(2);
	return 0;
}

int Transfer::start()
{
	StartServer(listen_ip, listen_port);
}

void Transfer::OnRecv(int _sockfd, PDUBase * _base)
{
	m_thread_pool->enqueue(&Transfer::ProcessBusiMsg, this, _sockfd, _base);
}

void Transfer::OnConn(int _sockfd)
{
}

void Transfer::OnDisconn(int _sockfd)
{
	//com disconn
    LOGD("disconnect sockfd:%d",_sockfd);
	{
		std::lock_guard<std::recursive_mutex> lock(m_node_list_mutex);
		for (auto it = m_node_list.begin(); it != m_node_list.end(); it++) {
			Node* node = *it;
            LOGD("sockfd:%d",node->fd);
			if (node->fd == _sockfd) {
				LOGD("node(node_id:%d,ip(%s),port(%d) disconnect", node->id, node->ip.c_str(), node->port);
				node->fd = -1;
				node->state = STATE_CONNECT;
				break;
			}
		}
	}
	CloseFd(_sockfd);
}

void Transfer::OnSendFailed(PDUBase & _data)
{
}

void Transfer::ProcessBroadcast(int _sockfd, PDUBase & _base)
{
   
	TransferBroadcastNotify broadcast;

	if (!broadcast.ParseFromArray(_base.body.get(), _base.length)) {
		LOGERROR(_base.command_id, _base.seq_id, "route broadcast parse fail");
		return;
	}
	auto& user_list = broadcast.user_id_list();
	const std::string& body = broadcast.body();
	const std::string&  msg_id = broadcast.msg_id();
	int  expire = broadcast.expire();
	
	std::string channel_msg = "channel_msg:" + msg_id;
    LOGD("recv broadcast msg msg_id:%s",msg_id.c_str());
	//quickly classification by com node id
	std::map<int, google_list_u32> ml;
	{
		std::lock_guard<std::recursive_mutex> lock(m_user_map_mutex);
		for (auto it = user_list.begin(); it != user_list.end(); it++) {
			auto user_it = m_user_map.find(*it);
			User* user = NULL;
			if (user_it != m_user_map.end()) {
				user = user_it->second;
				if (user->m_status == USER_STATE_ONLINE) {
					auto uit = ml.find(user->m_nid);
					if (uit != ml.end()) {
						uit->second.Add(user->m_id);
					}
					else {
						google_list_u32 li;
						li.Add(user->m_id);
						ml.insert(std::map<int, google_list_u32>::value_type(user->m_nid, li));

					}
					continue;
				}
			}
			
			//offline msg handler
			/*auto it = m_msg_map.find(msg_id);
			if (it == m_msg_map.end()) {
			PDUBase* base = new PDUBase;
			*base = _base;
			BroadcastMsgInfo* msginfo = new BroadcastMsgInfo;
			msginfo->expire = expire;
			msginfo->broadcast = broadcast;
			msginfo->pdu = base;
			m_msg_map[msg_id] = msginfo;
			}

			user->push(msg_id,expire);*/

			std::string userid = "userid:" + std::to_string(*it);
			redis_client.InsertBroadcastOfflineIMtoRedis(userid, channel_msg);
			
		}
		
	}


	//send msg
	
	for (auto it = ml.begin(); it != ml.end(); it++) {
		int sockfd = GetSockByNodeID(it->first);
		if (sockfd == -1) {
			continue;
		}
		auto user_list= broadcast.mutable_user_id_list();
		*user_list=it->second;

		std::shared_ptr<char> body(new char[broadcast.ByteSize()], carray_deleter);
		broadcast.SerializeToArray(body.get(), broadcast.ByteSize());
		_base.body = body;
		_base.length = broadcast.ByteSize();
		
        LOGD("send sockfd:%d",sockfd);
		Send(sockfd, _base);
	}
}


void Transfer::ProcessBusiMsg(int _sockfd, PDUBase * _base)
{
	int cmd = _base->command_id ;
	//_base->command_id= _base->command_id & 0xffffffff;
	switch (cmd) {
	case IMCHAT_PERSONAL_NOTIFY:
	case BULLETIN_NOTIFY:
		ProcessBroadcast(_sockfd, *_base);
		break;
	default:
		LOGE("unknow cmd:%d", cmd);
		break;
	}
	delete _base;
}

void Transfer::OnRoute(PDUBase * _base)
{
	//m_thread_pool->enqueue(&Transfer::ProcessRouteMsg, this, _base);
	ProcessRouteMsg(_base);
}

void Transfer::ProcessRouteMsg(PDUBase * _base)
{
	switch (_base->command_id) {
	case ROUTE_BRAODCAST:
		ProcessRouteBroadcast(*_base);
		break;
	case CID_USER_STAT_PUSH_REQ:
		ProcessUserState(*_base);
		break;

	default:
		LOGE("unknow cmd:%d", _base->command_id);
		break;
	}
	delete _base;
}

void Transfer::ProcessRouteBroadcast(PDUBase& _base)
{
	LOGD("recv route broadcast");
	RouteBroadcast broadcast;

	if (!broadcast.ParseFromArray(_base.body.get(), _base.length)) {
		LOGERROR(_base.command_id, _base.seq_id, "route broadcast parse fail");
		return;
	}
	const auto & service_list = broadcast.service_list();
	for (auto it = service_list.begin(); it != service_list.end(); it++) {
		const ServiceInfo& info = *it;
		int   id = info.id();
		const std::string ip = info.ip();
		short port = info.port();
		ServiceType service_type = info.service_type();
		if (service_type != COM_SERVER) {
			continue;
		}
		std::lock_guard<std::recursive_mutex> lock(m_node_list_mutex);
		auto res = std::find_if(m_node_list.begin(), m_node_list.end(), [&](const Node* node) {
			return id == node->id;
		});

		//add node
		if (res == m_node_list.end()) {
			Node* node = new Node;
			node->id = id;
			node->ip = ip;
			node->port = port;
			node->state = STATE_CONNECT;
			node->type = service_type;
			
			m_node_list.push_back(node);
			LOGD("serivce (ip:%s,port:%d,type:%d) add", ip.c_str(), port, service_type);
		}
	}
}

void Transfer::ProcessUserState(PDUBase & _base)
{
	RouteUsersStateBroadcast  user_state;
	if (!user_state.ParseFromArray(_base.body.get(), _base.length)) {
		LOGERROR(_base.command_id, _base.seq_id, "user state parse fail");
		return;
	}
	
	auto& user_list = user_state.user_state_list();
	for (auto it = user_list.begin(); it != user_list.end(); it++) {
		int user_id = it->user_id();
		int state = it->state();
		int node_id = it->node_id();
        const char* tip=state==0? "online":"offline";

		LOGD("%s node_id:%d,user_id:%d ",tip,node_id, user_id );
		std::lock_guard<std::recursive_mutex> lock(m_user_map_mutex);
		User* user = FindUser(user_id);
		if (user) {
			user->SetUserInfo(node_id, state);
		}
		else {
			AddUser(user_id, node_id, state);
		}
		//handler offline msg
		if (state == USER_STATE_ONLINE) {

			//HandlerOfflineMsg(user);
		}
	}
}

void Transfer::AddNode(std::string & ip, short port, std::string & id)
{
}

Node * Transfer::FindNode(std::string & id)
{
	return nullptr;
}


int Transfer::GetSockByNodeID(int node_id)
{
	std::lock_guard<std::recursive_mutex> lock(m_node_list_mutex);
	int fd = -1;
	std::for_each(m_node_list.begin(), m_node_list.end(), [&](Node* node) {
		if (node->id == node_id) {
			fd = node->fd;
		}
	});
	return fd;
}

User * Transfer::FindUser(int user_id)
{
	std::lock_guard<std::recursive_mutex> lock(m_user_map_mutex);
	User* user = NULL;
	std::for_each(m_user_map.begin(), m_user_map.end(), [=, &user](std::pair<const int, User*>& it) {if (user_id == it.first) { user = it.second; }});

	return user;
}

User* Transfer::AddUser(int user_id, int nid, int status)
{
	User* user = new User(user_id, nid, status);
	std::lock_guard<std::recursive_mutex> lock(m_user_map_mutex);
	m_user_map[user_id] = user;
	return user;
}

void Transfer::HandlerOfflineMsg(User * user)
{
	std::list<std::string> msg_list;
	int size=user->GetOfflineMsg(msg_list);
	if (size) {
		std::lock_guard<std::recursive_mutex> lock(m_msg_map_mutex);
		for (auto it = msg_list.begin(); it != msg_list.end(); it++){
			auto mit = m_msg_map.find(*it);
			if (mit != m_msg_map.end()) {
				PDUBase* _base = mit->second->pdu;
				TransferBroadcastNotify& broadcast = mit->second->broadcast;
				auto user_list = broadcast.mutable_user_id_list();
				user_list->Add(user->m_id);

				std::shared_ptr<char> body(new char[broadcast.ByteSize()], carray_deleter);
				broadcast.SerializeToArray(body.get(), broadcast.ByteSize());
				_base->body = body;
				_base->length = broadcast.ByteSize();

				//Send(sockfd, *_base);
			}
		}
	}
}


void Transfer::Timer(int fd, short mask, void * clientData)
{
	static uint64_t m_cronloops = 0;
	Transfer* tf = static_cast<Transfer*>(clientData);
	if (tf) {
		tf->Cron();
		run_with_period(5 * 60 * 1000) {
			tf->count();
		}
	}
	++m_cronloops;
}

void Transfer::Cron()
{
	static long long m_cronloops = 1;
	//connect com 
	{
		std::lock_guard<std::recursive_mutex> lock(m_node_list_mutex);
		for(auto it=m_node_list.begin();it!=m_node_list.end();it++){
			Node* node = *it;
			if (node->state == STATE_CONNECT && node->type== COM_SERVER) {
				int fd = StartClient(node->ip, node->port);
				if (fd < 0) {
					LOGE("connect fail");
					continue;
				}
				node->fd = fd;
				node->state = STATE_CONNECTED;
                LOGD("connect service(ip:%s,port:%d) success",node->ip.c_str(),node->port);
			}
		}
	}
    return;
	//remove expire msg
	
	run_with_period(2000) {
		CheckCache();
	}
	
}

void Transfer::CheckCache()
{
	time_t t = time(0);
	g_time_cache = t;
	{
		std::lock_guard<std::recursive_mutex> lock(m_msg_map_mutex);
		auto it = m_msg_map.begin();
		int size = 0;
		while (it != m_msg_map.end()) {

			BroadcastMsgInfo* msginfo = it->second;
			if (msginfo->expire >= t) {
				delete msginfo->pdu;
				delete msginfo;
				m_msg_map.erase(it++);
				++size;
				if (size >= REMOVE_MSG_CACHE_PER_TIME) {
					break;
				}
			}
			else {
				++it;
			}
		}

	}
	//remove user cache
	{
		std::lock_guard<std::recursive_mutex> lock(m_user_map_mutex);
		for (auto it = m_user_map.begin(); it != m_user_map.end(); it++) {
			User* user = it->second;
			user->CheckCache();
		}
	}

}

void Transfer::count()
{
	int onlines = 0;
	std::lock_guard<std::recursive_mutex> lock(m_user_map_mutex);
	for (auto it = m_user_map.begin(); it != m_user_map.end(); it++) {
		User* user = it->second;
		if (user->m_status == USER_STATE_ONLINE) {
			++onlines;
		}
	}
	LOGD("onlines users(%d)", onlines);
}

