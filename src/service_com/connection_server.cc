#include "connection_server.h"
#include "log_util.h"
#include "config_file_reader.h"
#include "YouMai.Chat.pb.h"
#include "YouMai.Bulletin.pb.h"
#include "time_util.h"
#include "logic_util.h"
#include "save_im_object.h"
#include <json/json.h>
#include <unistd.h>
#include "Base64.h"
#include "YouMai.Route.pb.h"
#include "YouMai.Transfer.pb.h"

using namespace com::proto::chat;
using namespace com::proto::route;
using namespace com::proto::transfer;
using namespace com::proto::bulletin;
void reportOnliners(ConnectionServer* s){
    s->reportOnliners();
}
void ConnectionServer:: check_msg(ConnectionServer* s){
    //return;
    while(1){
        sleep(2);
    s->check_send_msg();
    }
}
ConnectionServer::ConnectionServer() {
	onliners_ = 0;
	m_cur_index_ = 0;
	m_lastTime = 0;
}

ConnectionServer * ConnectionServer::getInstance()
{
	static ConnectionServer instance;
	return &instance;
}

int ConnectionServer::init()
{
	// 读取route配置信息，进行连接
	ConfigFileReader reader(CONF_PUBLIC_URL);
	ip_ = reader.ReadString(CONF_COM_IP);
	port_ = reader.ReadInt(CONF_COM_PORT);
	loadbalance_client_.SetRegistInfo(ip_,port_ );
	route_client_.SetRegistInfo(reader.ReadString(CONF_COM_IP), reader.ReadInt(CONF_COM_PORT));

	loadbalance_ip_ = reader.ReadString(CONF_LOADBALANCE_IP);
	loadbalance_port_ = reader.ReadInt(CONF_LOADBALANCE_PORT);
	route_ip_ = reader.ReadString(CONF_ROUTE_IP);
	route_port_ = reader.ReadInt(CONF_ROUTE_PORT);
	
	redis_client.Init_Pool(reader.ReadString(CONF_REDIS_IP), reader.ReadInt(CONF_REDIS_PORT),
		reader.ReadString(CONF_REDIS_AUTH) );
	redis_client.SetKeysExpire(reader.ReadInt(CONF_REDIS_EXPIRE));


	
	
	m_chat_msg_process.init(ProcessClientMsg);
	

	m_common_msg_process.init(ProcessClientMsg, 2);
	
	CreateTimer(1000, Timer, this);
	ack_time_ = MSG_ACK_TIME;
	std::thread check_msg_(check_msg,this);
	//std::thread check_msg_(std::bind(&ConnectionServer::check_send_msg,this));
	check_msg_.detach();
	// tcp阻塞连接，用于查询
	//  block_tcp_client_.Connect(query_ip_.c_str(), query_port_, false);
	m_offline_works = new ThreadPool(1); //save offline msg threads
	m_route_works = new ThreadPool(2); //handler route msg;
	m_storage_msg_works = new ThreadPool(1);//save msg redis;
    sleep(1);
	if (route_client_.Connect(route_ip_.c_str(), route_port_)) {
		LOGE("connect route server(%s:%d) fail",route_ip_.c_str(),route_port_);
		return -1;
	}
	if (loadbalance_client_.Connect(loadbalance_ip_.c_str(), loadbalance_port_)) {
		LOGE("connect loadbalance server fail");
		return -1;
	}
}

void ConnectionServer::start() {
	
    m_chat_msg_process.start();
	
	m_common_msg_process.start();
	sleep(1);
	LOGD("connect server listen on %s:%d", ip_.c_str(), port_);
    TcpServer::StartServer(ip_, port_);
}

extern int total_recv_pkt;
extern int total_user_login;

void ConnectionServer::OnRecv(int _sockfd, PDUBase* _base) {
	int cmd = _base->command_id;
	int index = 0;
	if (_base->terminal_token<0) {
		LOGD("==========sockfd:%d===userid:%d==========>in cmd:%d", _sockfd, _base->terminal_token, _base->command_id);
		delete _base;
		return;
	}
	if (PreProcessPack(_sockfd, _base->terminal_token, *_base) == -1) {
		LOGE("PreProcessPack fail");
		delete _base;
		return;
	}

	switch (cmd) {
	case HEART_BEAT:
	case USER_LOGIN:
	case USER_LOGOFF:
		m_common_msg_process.addJob(_sockfd, _base);
		break;

	case IMCHAT_PERSONAL:
	case IMCHAT_PERSONAL_NOTIFY:
	case BULLETIN_NOTIFY:
	case IMCHAT_PERSONAL_ACK:
		m_chat_msg_process.addJob(_sockfd, _base);
		break;
    default:
		LOGE("unknow cmd(%d)", cmd);
		delete _base;
		break;
	}

	//printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~<<<<<出\n");
}

void ConnectionServer::OnConn(int _sockfd) {
    //LOGD("建立连接fd:%d", _sockfd);
}

void ConnectionServer::OnDisconn(int _sockfd) {

	CloseFd(_sockfd);

	int userid = 0;
	if (!find_userid_by_sockfd(_sockfd, userid, true)) {
		LOGE("not find sockfd:%d", _sockfd);
		return;
	}
	ClientObject client;
	if (!find_client_by_userid(userid, client, true)) {
        LOGE("not find user_id:%d",userid);
		return;
	}
	--onliners_;
	set_user_state(userid, OnlineStatus_Offline);
	LOGT("user[phone:%s,userid:%d,fd:%d] disconnect", client.phone_.c_str(), userid, _sockfd);

	// 通知路由服务器登出
	User_LogOff log_off;
	log_off.set_user_id(userid);
	PDUBase pack;
	std::shared_ptr<char> body(new char[log_off.ByteSize()], carray_deleter);
	log_off.SerializeToArray(body.get(), log_off.ByteSize());
	pack.body = body;
	pack.command_id = USER_LOGOFF;
	pack.length = log_off.ByteSize();
	pack.seq_id = 0;
	pack.terminal_token = userid;
	route_client_.Send(pack);
}

void ConnectionServer::OnSendFailed(PDUBase &_data) {
	LOGE("send msg to user[user_id:%d] fail",_data.terminal_token);

	std::lock_guard<std::recursive_mutex> lock_1(send_failed_cache_queue_mutex_);
	auto it = send_failed_cache_queue_.find(_data.terminal_token);
	if (it != send_failed_cache_queue_.end()) {
		it->second.push_back(_data);
	}
	else {
		std::list<PDUBase> resend_list;
		resend_list.push_back(_data);
		send_failed_cache_queue_[_data.terminal_token] = resend_list;
	}
}

int ConnectionServer::PreProcessPack(int _sockfd, int _userid, PDUBase &_base) {
	//LOGDEBUG(_base.command_id, _base.seq_id, "预处理包");
    int cmd=_base.command_id;
	if (cmd == USER_LOGIN || cmd==IMCHAT_PERSONAL_NOTIFY || cmd==BULLETIN_NOTIFY || cmd==USER_LOGOFF) {
		return 0;
	}

	int old_user_sockfd = 0;
	ClientObject client;
	if (find_client_by_userid(_userid, client)) {
		old_user_sockfd = client.sockfd_;
        int status=client.online_status_;
		client.sockfd_ = _sockfd;
        client.online_status_=OnlineStatus_Connect;
		client.online_time = time(0);
		{
			std::lock_guard<std::recursive_mutex> lock_1(user_map_mutex_);
			user_map_[client.userid_] = client;
		}
		{
			std::lock_guard<std::recursive_mutex> lock_1(socket_userid_mutex_);
			socket_userid_[_sockfd] = client.userid_;
		}
		if (status == OnlineStatus_Offline) {
			LOGD("命中缓存，但处于离线状态，重置为在线 user[user_id:%d,phone:%s,fd:%d]", _userid, client.phone_.c_str(), _sockfd);
            ++onliners_;
			
		}
		else if (status == OnlineStatus_Connect) {
			if (old_user_sockfd != 0 && old_user_sockfd != _sockfd) {
				CloseFd(old_user_sockfd);
				std::lock_guard<std::recursive_mutex> lock_1(socket_userid_mutex_);
				socket_userid_.erase(old_user_sockfd);
				
				LOGT("断线重连，关闭假死FD; user[user_id:%d; phone:%s],oldfd:%d; newfd:%d;", _userid, client.phone_.c_str(), old_user_sockfd, _sockfd);
				
			}
			else {
				return 0;
			}
		}
		RegistUserToRoute(client);
	}
	else { //use for server restart
		// 登录助手
		LOGD("user[user_id:%d] login from LoginUtil", _userid);
		 LoginUtil(_sockfd, _userid);
		
	}

	// thread_pool.enqueue(&ConnectionServer::ResendFailedPack, this, _sockfd, _userid);
	//thread_pool.enqueue(&ConnectionServer::ConsumeHistoryMessage, this, _userid);
	PDUBase* pdu = new PDUBase;
	pdu->command_id = IM_OFFLINE_MSG;
	pdu->terminal_token = _userid;
	m_chat_msg_process.addJob(_sockfd, pdu);
	ResendFailedPack(_sockfd, _userid);
	return 0;
}

void ConnectionServer::ProcessClientMsg(int _sockfd, PDUBase* _base)
{
	ConnectionServer* pInstance = ConnectionServer::getInstance();
	switch (_base->command_id) {
	case HEART_BEAT:
		pInstance->ProcessHeartBeat(_sockfd, *_base);
		break;
	case USER_LOGIN:
		++total_user_login;
		pInstance->ProcessUserLogin(_sockfd, *_base);
		//          _ProcessUserLogin(_sockfd,&_base);
		break;

	case USER_LOGOFF :
		pInstance->ProcessUserLogout(_sockfd, *_base);
		break;
	case IMCHAT_PERSONAL:
		pInstance->ProcessIMChat_Personal(_sockfd, *_base);
		break;
	case IM_OFFLINE_MSG:
		pInstance->ConsumeHistoryMessage(_base->terminal_token);
		break;
	case IMCHAT_PERSONAL_ACK:
		pInstance->ProcessChatMsg_ack(_sockfd, *_base);
        break;
	case IMCHAT_PERSONAL_NOTIFY:
		pInstance->ProcessIMChat_broadcast(_sockfd, *_base);
		break;
	case BULLETIN_NOTIFY:
		pInstance->ProcessBulletin_broadcast(_sockfd, *_base);
		break;
	case 0:
		pInstance->ack_timeout_handler(_base->terminal_token);
		break;
	default:
		LOGE("unknow cmd:%d", _base->command_id);
		break;
	}
	delete _base;
	
}
/*
* 心跳
* 回复空包
* 线程处理失败消息列表
*/
void ConnectionServer::ProcessHeartBeat(int _sockfd, PDUBase _pack) {
	//    LOGDEBUG(_pack.command_id, _pack.seq_id, "心跳userid:%d,fd:%d", _pack.terminal_token, _sockfd);

	Heart_Beat heart;
	heart.ParseFromArray(_pack.body.get(), _pack.length);

	// 回复心跳包
	Heart_Beat_Ack heart_ack;
	_pack.command_id = HEART_BEAT_ACK;
	std::shared_ptr<char> body(new char[heart_ack.ByteSize()], carray_deleter);
	heart_ack.SerializeToArray(body.get(), heart_ack.ByteSize());
	_pack.body = body;
	_pack.length = heart_ack.ByteSize();
	Send(_sockfd, _pack);

	ResendFailedPack(_sockfd, _pack.terminal_token);
}

void ConnectionServer::ProcessUserLogin(int _sockfd, PDUBase& _base) {
	//LOGI("===开始处理用户登录timestamp:%d===", TimeUtil::timestamp_int());

	User_Login login;
	if (!login.ParseFromArray(_base.body.get(), _base.length)) {
		LOGERROR(_base.command_id, _base.seq_id, "User_Login包解析错误");
		return;
	}
    int version=0;
    if(login.has_version()){
        version=login.version();
        LOGD("version:%d",version);
    }
	//int login_result=ERRNO_CODE_OK;
	int login_result = BuildUserCacheInfo(_sockfd, login,version);
	if (login_result == ERRNO_CODE_OK) {
		ResendFailedPack(_sockfd, login.user_id());
		ConsumeHistoryMessage(login.user_id());
		++onliners_;
	}
	
	//LOGD("回复登录结果 %d", login_result);
	User_Login_Ack login_ack;
	login_ack.set_errer_no((ERRNO_CODE)login_result);
	ResetPackBody(_base, login_ack, USER_LOGIN_ACK);
	Send(_sockfd, _base);
	LOGD("---------->user [userid:%d, phone:%s, sessionid:%s,fd:%d] login(%d) version(%d)------loginers[%d]--user_map[%d]------->", login.user_id(), login.phone().c_str(),  login.session_id().c_str(), _sockfd, login_result,version, (int)onliners_, user_map_.size());
	//LOGI("~~~结束处理用户登录timestamp:%d~~~", TimeUtil::timestamp_int());
}

void ConnectionServer::ProcessUserLogout(int _sockfd, PDUBase & _base)
{
	User_LogOff logout;
	if (!logout.ParseFromArray(_base.body.get(), _base.length)) {
		LOGERROR(_base.command_id, _base.seq_id, "user logout pkt parse fail");
		return;
	}
	
	int userid;
	find_userid_by_sockfd(_sockfd, userid, true);
	userid = logout.user_id();
	ClientObject client;
	if (!find_client_by_userid(userid, client, true)) {
        LOGE("not find user_id(%d)",userid);
		return;
	}
	--onliners_;
	LOGD("user[phone:%s,userid:%d,fd:%d ]logout", client.phone_.c_str(), userid, _sockfd);
	// 通知路由服务器登出
	User_LogOff log_off;
	log_off.set_user_id(userid);
	PDUBase pack;
	std::shared_ptr<char> body(new char[log_off.ByteSize()], carray_deleter);
	log_off.SerializeToArray(body.get(), log_off.ByteSize());
	pack.body = body;
	pack.command_id = USER_LOGOFF;
	pack.length = log_off.ByteSize();
	pack.seq_id = 0;
	pack.terminal_token = userid;
	route_client_.Send(pack);
	set_user_state(userid, OnlineStatus_Offline);
	CloseFd(_sockfd);
}

/*
* 处理用户发送的IM消息
* 注意：用户发送消息时只有自己的userid和对方的手机号，所以其他信息需要查询获得
*/
extern std::atomic_int offline_msg;
void ConnectionServer::ProcessIMChat_Personal(int _sockfd, PDUBase&  _base) {
	IMChat_Personal im;
	Device_Type device_type = DeviceType_UNKNOWN;

	if (!im.ParseFromArray(_base.body.get(), _base.length)) {
		LOGERROR(_base.command_id, _base.seq_id, "IMChat_Personal包解析错误");
		ReplyChatResult(_sockfd, _base, ERRNO_CODE_DATA_SRAL);
		return;
	}

	ClientObject client;
	if (find_client_by_userid(_base.terminal_token, client)) {
		im.set_src_phone(client.phone_);
		device_type = client.device_type_;
	}
	else {
		ReplyChatResult(_sockfd, _base, ERRNO_CODE_NOT_HUXIN_USER);
		LOGERROR(_base.command_id, _base.seq_id, "发送方userid:%d为非呼信用户", _base.terminal_token);
		return;
	}
	/*if (client.online_status_ != OnlineStatus_Connect) {
		LOGE("user[user_id:%d,phone:%s not login,send msg fail", _base.terminal_token, im.src_phone().c_str());
		OnDisconn(_sockfd);
		return;
	}*/

	if (!im.has_target_phone()) {
		LOGERROR(_base.command_id, _base.seq_id, "ProcessIMChat_Personal, !has_target_phone");
		ReplyChatResult(_sockfd, _base, ERRNO_CODE_INVALID_IM_CHAT_TARGET_USER);
		return;
	}
	if (!im.has_body()) {
		LOGERROR(_base.command_id, _base.seq_id, "ProcessIMChat_Personal, !has_body");
		ReplyChatResult(_sockfd, _base, ERRNO_CODE_INVALID_IM_CHAT_EMPTY_BODY_NOT_ALLOWED);
		return;
	}


    LOGD("time:%ld",im.timestamp());
	// 以服务器收到消息的时间为准
	im.set_timestamp(TimeUtil::timestamp_int());
	uint64_t msg_id = getMsgId();
	LOGI("IM type:%d，from:%s to:%s (msg_id:%ld)", im.content_type(), im.src_phone().c_str(), im.target_phone().c_str(), msg_id);
	if (im.content_type() == 1) {
		LOGD("content msg:%s", im.body().c_str());
	}
	int dst_userid = 0;
	int errno_code = PhoneQueryUserId(im.target_phone(), dst_userid);
	
	if (errno_code == ERRNO_CODE_OK && dst_userid != 0) {
		// 检测目标用户是否在线
		bool is_target_online = false;
		bool local_online = false;
		ClientObject dst_client;
		if (find_client_by_userid(dst_userid, dst_client)) {
			if (dst_client.online_status_ == OnlineStatus_Connect) {
				is_target_online = true;
				local_online = true;
			}
		}
		else {
			is_target_online = IsUserOnline(dst_userid);
		}

		ReplyChatResult(_sockfd, _base, ERRNO_CODE_OK, is_target_online,msg_id);
		
		im.set_target_user_id(dst_userid);
		im.set_target_user_type(USER_TYPE_PERSONAL);
		im.set_msg_id(msg_id);
		ResetPackBody(_base, im, IMCHAT_PERSONAL);

		if (is_target_online) {
			LOGI("user[user_id:%d,phone:%s]%s", dst_userid, im.target_phone().c_str(), "online");
			if (local_online) {
				_base.terminal_token = dst_userid;
				ProcessIMChat_fromRoute(_base);
			}
			else {
				//LOGDEBUG(_base.command_id, _base.seq_id, "消息发送到路由");
				std::string session = LogicUtil::build_session_id(im.src_usr_id(), IMCHAT_PERSONAL, dst_userid);
				route_client_.RouteTarget(_base, dst_userid, session, IMCHAT_PERSONAL, true);
			}

		}
		else {
			++offline_msg;
			IMChat_Personal_Notify im_notify;
			im_notify.set_allocated_imchat(&im);
			std::shared_ptr<char> body(new char[im_notify.ByteSize()], carray_deleter);
			im_notify.SerializeToArray(body.get(), im_notify.ByteSize());
			_base.body = body;
			_base.length = im_notify.ByteSize();
			_base.seq_id = 0;
			im_notify.release_imchat();
			SaveOfflineMsg(dst_userid, _base);
			LOGI("user[user_id:%d,phone:%s]  offline，save offline msg", dst_userid, im.target_phone().c_str());
		}
		
	}
	else {
		LOGE("query user_id fail err code(%d)", errno_code);
		ReplyChatResult(_sockfd, _base, (ERRNO_CODE)errno_code,false,msg_id);
		//return;
	}
    //return;
	//LOGDEBUG(_base.command_id, _base.seq_id, "消息入Redis库");
	SaveIMObject*  object = new SaveIMObject;
	object->id_ = msg_id;
	object->pid_ = 0;
	object->brand_ = 0;
	object->content_type_ = im.content_type();
	object->delete_flag_ = 0;
	object->reply_flag_ = 0;
	object->collect_flag_ = 0;
	object->sender_userid_ = im.src_usr_id();
	object->sender_phone_ = im.src_phone();
	object->recver_userid_ = im.target_user_id();
	object->recver_phone_ = im.target_phone();
	object->body_ = im.body();
	object->add_time_ = TimeUtil::timestamp_datetime();
	m_storage_msg_works->enqueue(&ConnectionServer::storage_common_msg, this, object);
	

	//LOGDEBUG(_base.command_id, _base.seq_id, "推送%d", device_type);
	if (device_type == DeviceType_IPhone || device_type == DeviceType_IPad) {
		Json::FastWriter  fwriter;
		Json::Value root;
		root["message"] = Json::Value(im.body());
		root["targetPhone"] = Json::Value(im.target_phone());
		root["msisdn"] = Json::Value(im.src_phone());
		root["imType"] = im.content_type();
		std::string* object = new std::string;
		*object = fwriter.write(root);
		m_storage_msg_works->enqueue(&ConnectionServer::storage_apple_msg, this, object);
	}
}


void ConnectionServer::ProcessChatMsg_ack(int _sockfd, PDUBase & _base) {
	IMChat_Personal_recv_Ack ack;
	if (!ack.ParseFromArray(_base.body.get(), _base.length)) {
		LOGERROR(_base.command_id, _base.seq_id, "Broadcast parse fail");
		return;
	}
	int user_id = ack.user_id();
	ClientObject* target;
	if (!find_client_by_userid(user_id, target)) {
		return;
	}
	long long cur_ms = get_mstime();
	target->ack_time = cur_ms;
	uint64_t msg_id = ack.msg_id();
	LOGD("recv user_id(%d) ack msg(%ld)", user_id, msg_id);
	
//	std::lock_guard<std::recursive_mutex> lock1(m_send_msg_mutex_);
//	CAutoRWLock lock(&m_rwlock_, 'w');
	auto it = m_send_msg_map_.find(user_id);
	if (it != m_send_msg_map_.end()) {//last msg is waiting for ack
		if (!it->second.empty()) {
			Ackmsg* ackmsg=it->second.front();
			if (ackmsg->msg_id != msg_id && ackmsg->msg_id!=0) {
				LOGD("user_id(%d) rsp error ack msg_id(%ld)", user_id, msg_id);//because of user having recving the pkt even if msg_id not true. we continue send it next msg due to the using online
				return ;
			}
			LOGD("msg_id(%ld)  ack latency time %d ms,send latency time:%d ms", msg_id,cur_ms- target->send_time, cur_ms - ackmsg->ms);
			delete ackmsg;
			it->second.pop_front();
            delete_ack_msg(user_id,msg_id);
			target->send_pending = 0;
			if (!it->second.empty()) {
				ackmsg = it->second.front();
                LOGD("begin send msg_id(%ld)",ackmsg->msg_id);
				Send(target->sockfd_, ackmsg->pdu);//ignore send result,if fail 
				target->send_pending = 1;
				target->send_time = cur_ms;
				record_waiting_ackmsg(user_id, ackmsg->msg_id);
			}
		}
	}

}

void ConnectionServer::ProcessIMChat_broadcast(int _sockfd, PDUBase & _base)
{
	TransferBroadcastNotify broadcast;
	if (!broadcast.ParseFromArray(_base.body.get(), _base.length)) {
		LOGERROR(_base.command_id, _base.seq_id, "Broadcast parse fail");
		return;
	}

	std::string msg_id = broadcast.msg_id();
	auto& body = broadcast.body();
	IMChat_Personal_Notify im_notify;

	if (!im_notify.ParseFromArray(body.c_str(), body.length())) {
		LOGERROR(_base.command_id, _base.seq_id, "Broadcast parse fail");
		return;
	}
	LOGI("recv broadcat chat msg [msg_id:%s]", msg_id.c_str());

	std::string channel_msg = "channel_msg:" + msg_id;
	::com::proto::chat::IMChat_Personal* im = im_notify.mutable_imchat();
    if(!im->has_body()){
        LOGE("not body");
    }
   
	const auto& user_list = broadcast.user_id_list();
	for (auto it = user_list.begin(); it != user_list.end(); it++) {
		ClientObject client;
	
		if (find_client_by_userid(*it, client)) {
			if (client.online_status_ == OnlineStatus_Connect) {
                LOGI("broadcast chat msg to user_id(%d)", *it);
				im->set_target_user_id(*it);
				im->set_target_phone(client.phone_);
				_base.terminal_token = *it;
				ResetPackBody(_base, im_notify, IMCHAT_PERSONAL);
				if (client.version == VERSION_0) {
					if (!Send(client.sockfd_, _base)) {
						OnSendFailed(_base);
					}
				}
				else {
					need_send_msg(*it, client.sockfd_, _base, atoi(msg_id.c_str()));
				}
                continue;
			}
		}
		LOGD("save broadcast offline msg user_id(%d)", *it);
		std::string userid = "userid:" + std::to_string(*it);
		m_storage_msg_works->enqueue(&ConnectionServer::storage_broadoffline_msg, this, userid,channel_msg);
	}
}

void ConnectionServer::ProcessBulletin_broadcast(int _sockfd, PDUBase & _base)
{
	TransferBroadcastNotify broadcast;
	if (!broadcast.ParseFromArray(_base.body.get(), _base.length)) {
		LOGERROR(_base.command_id, _base.seq_id, "Broadcast parse fail");
		return;
	}

	std::string msg_id = broadcast.msg_id();
	auto& body = broadcast.body();
	std::shared_ptr<char> pbody(new char[body.length()], carray_deleter);
	memcpy(pbody.get(), body.c_str(), body.length());
	_base.body= pbody;
    _base.length=body.length();
	LOGI("recv broadcat bulletin  msg [msg_id:%s]", msg_id.c_str());
	std::string channel_msg = "channel_msg:" + msg_id;
	Bulletin_Notify  notify;
    if (!notify.ParseFromArray(body.c_str(), body.length())) {
        LOGE("broadcast bulletin parse fail");
        return;
    }
	const auto& user_list = broadcast.user_id_list();
	for (auto it = user_list.begin(); it != user_list.end(); it++) {
		ClientObject client;
		if (find_client_by_userid(*it, client)) {
			if (client.online_status_ == OnlineStatus_Connect) {

				LOGI("broadcast bulletin  msg to user_id(%d)", *it);
				_base.terminal_token = *it;
				if (client.version == VERSION_0 || client.version == VERSION_1 && _base.command_id == BULLETIN_NOTIFY) {
					if (!Send(client.sockfd_, _base)) {
						OnSendFailed(_base);
					}
				}
				else {
					need_send_msg(*it, client.sockfd_, _base, atoi(msg_id.c_str()));
				}
				continue;
			}
		}
		LOGD("save bulletin offline msg user_id(%d)", *it);
		std::string userid = "userid:" + std::to_string(*it);
		m_storage_msg_works->enqueue(&ConnectionServer::storage_broadoffline_msg, this, userid, channel_msg);
	}
}
	
void ConnectionServer::SaveOfflineMsg_(int _userid, char* data,int len)
{
	char buffer[10240] = { 0 };
	
	if (base64_encode(buffer, data, len)) {
		redis_client.InsertOfflineIMtoRedis(_userid, buffer);
	}
	delete[] data;
}

void ConnectionServer::SaveOfflineMsg(int _userid, PDUBase & _base)
{
	char* data = NULL;
	//first check send queue
	auto mit = m_send_msg_map_.find(_userid);
	if (mit != m_send_msg_map_.end()) {
		std::list<Ackmsg*>& msg_list = mit->second;
		LOGD("user_id(%d) offline msg num:%d", _userid, msg_list.size());
		for (auto msg_it = msg_list.begin(); msg_it != msg_list.end(); msg_it++) {
			int len = OnPduPack((*msg_it)->pdu, data);
			if (len > 0) {
				m_offline_works->enqueue(&ConnectionServer::SaveOfflineMsg_, this, _userid, data, len);
			}
		//	SaveOfflineMsg(*it, (*msg_it)->pdu);//save timeout user msg
			delete *msg_it;

		}
		m_send_msg_map_.erase(_userid);
	}
	
	int len = OnPduPack(_base, data);
	if (len > 0) {
		m_offline_works->enqueue(&ConnectionServer::SaveOfflineMsg_, this, _userid, data, len);
	}
}

void ConnectionServer::OnRoute(PDUBase* _base) {
	m_route_works->enqueue(&ConnectionServer::ProcessRouteMsg, this, _base);
}

void ConnectionServer::ProcessRouteMsg(PDUBase * _base)
{
	switch (_base->command_id) {
	case IMCHAT_PERSONAL:
		//LOGDEBUG(_base.command_id, _base.seq_id, "COM进程收到ROUTE进程转发的IM数据");
		ProcessIMChat_fromRoute(*_base);
		break;
	case CID_USER_STAT_SYNC_REQ:
		ProcessUserStatSyncReq(*_base);
		break;
	case REGIST_RSP:
		ProcessRegistRsp(*_base);
		break;
	default:
		LOGDEBUG(_base->command_id, _base->seq_id, "COM进程收到ROUTE进程转发的未知数据，未处理");
		break;
	}
	delete _base;
}

void ConnectionServer::ProcessUserStatSyncRsp(RouteSyncUserStateRsp& rsp) {
	PDUBase pack;
	std::shared_ptr<char> body(new char[rsp.ByteSize()], carray_deleter);
	rsp.SerializeToArray(body.get(), rsp.ByteSize());
	pack.body = body;
	pack.command_id = CID_USER_STAT_SYNC_RSP;
	pack.length = rsp.ByteSize();
	pack.seq_id = 0;
	route_client_.Send(pack);
}
void ConnectionServer::ProcessUserStatSyncReq(PDUBase &_pack) {
	RouteSyncUserStateReq req;
	if (!req.ParseFromArray(_pack.body.get(), _pack.length)) {
		LOGERROR(_pack.command_id, _pack.seq_id, "ProcessUserStatSyncReq包解析错误");
		return;
	}
	int node_id = req.node_id();
	LOGD("user state sync req nid:%d", node_id);
	//send all onlines to route
	RouteSyncUserStateRsp rsp;
	auto user_list = rsp.mutable_user_state_list();
	int num = 0;
	std::lock_guard<std::recursive_mutex> lock_1(user_map_mutex_);
	for (auto it = user_map_.begin(); it != user_map_.end(); it++) {
		if (it->second.online_status_ == OnlineStatus_Connect) {
			
			++num;
			auto user_state= user_list->Add();
			user_state->set_state(USER_STATE_ONLINE);
			user_state->set_user_id(it->first);
			user_state->set_node_id(node_id);
			if (!(num % 2000)) {
				ProcessUserStatSyncRsp(rsp);
				user_list->Clear();
			}
		}
		
	}
	if (num % 2000) {
		ProcessUserStatSyncRsp(rsp);
	}
}


void ConnectionServer::ProcessRegistRsp(PDUBase &_pack)
{
	RegistRsp rsp;
	if (!rsp.ParseFromArray(_pack.body.get(), _pack.length)) {
		LOGERROR(_pack.command_id, _pack.seq_id, "ProcessUserStatSyncReq包解析错误");
		return;
	}
	int res = rsp.result();
	if (res == 0) {
		LOGD("regist fail,stop service");
		exit(0);
	}
	m_node_id_ = rsp.node_id();
	LOGD("regist success,node_id(%d)", m_node_id_);
	
}
void ConnectionServer::BroadUserState(int user_id, UserState state)
{
}



void ConnectionServer::RegistUsersToRoute() {

}

void ConnectionServer::ResetPackBody(PDUBase &_pack, google::protobuf::Message &_msg, int _commandid) {
    std::shared_ptr<char> body(new char[_msg.ByteSize()], carray_deleter);
    _msg.SerializeToArray(body.get(), _msg.ByteSize());
    _pack.body = body;
    _pack.length = _msg.ByteSize();
    _pack.command_id = _commandid;
}

void ConnectionServer::ProcessIMChat_fromRoute(PDUBase &_base) {
    IMChat_Personal im;
   
    if (!im.ParseFromArray(_base.body.get(), _base.length)) {
        LOGERROR(_base.command_id, _base.seq_id, "IMChat_Personal包解析错误");
        return;
    }

    ClientObject target;
    if (find_client_by_userid(im.target_user_id(), target)) {
        if (target.online_status_ == OnlineStatus_Connect) {
			IMChat_Personal_Notify im_notify;
			im_notify.set_allocated_imchat(&im);
			std::shared_ptr<char> body(new char[im_notify.ByteSize()], carray_deleter);
			im_notify.SerializeToArray(body.get(), im_notify.ByteSize());
			_base.body = body;
			_base.length = im_notify.ByteSize();
			_base.seq_id = 0;
			im_notify.release_imchat();
			
			if (target.version == VERSION_0) {//save send msg for waiting ack
				if (Send(target.sockfd_, _base)) {
					return;
				}
				
			}
			else {
				need_send_msg(im.target_user_id(), target.sockfd_, _base, im.msg_id());
				return;
			}
        } 
    } 
	LOGI("route:user[user_id:%d,phone:%s]  offline，save offline msg", im.target_user_id(), im.target_phone().c_str());
	SaveOfflineMsg(im.target_user_id(), _base);
   
}

void ConnectionServer::Timer(int fd, short mask, void * privdata)
{
	ConnectionServer* server = reinterpret_cast<ConnectionServer*>(privdata);
	if (!server) {
		return;
	}
	server->reportOnliners();

}

int ConnectionServer::BuildUserCacheInfo(int _sockfd, User_Login& _login,int version) {
    if (!_login.has_phone()) {
        return ERRNO_CODE_NOT_PHONE;
    }
    if (!_login.has_user_id() || _login.user_id() <= 0) {
        return ERRNO_CODE_USER_ID_ERROR;
    }
    if (!_login.has_session_id()) {
        return ERRNO_CODE_NOT_SESSIONID;
    }
    if (!redis_client.IsHuxinUser(_login.phone())) {
        return ERRNO_CODE_NOT_HUXIN_USER;
    }

    std::string userid = redis_client.GetUserId(_login.phone());
    if (userid != std::to_string(_login.user_id())) {
        LOGE("用户登录userid不匹配，%d %s", _login.user_id(), userid.c_str());
        return ERRNO_CODE_USER_ID_ERROR;
    }
    std::string sessid = redis_client.GetSessionId(_login.phone());
    if (sessid != _login.session_id()) {
        LOGE("用户登录sessionid不匹配，%s %s", _login.session_id().c_str(), sessid.c_str());
        return ERRNO_CODE_ERR_SESSIONID;
    }

	ClientObject client;
    if(find_client_by_userid(_login.user_id(), client)) {
        if(client.online_status_ == OnlineStatus_Connect){
			LOGD("relogin user[user_id:%d ,phone:%s", client.userid_, client.phone_.c_str());
            CloseFd(client.sockfd_);
            std::lock_guard<std::recursive_mutex> lock_1(socket_userid_mutex_);
            socket_userid_.erase(client.sockfd_);
        }
    }
  
	client.sockfd_ = _sockfd;
	client.userid_ = _login.user_id();
	client.phone_ = _login.phone();
	client.online_status_ = OnlineStatus_Connect;
	client.version = version;
	client.session_id_ = _login.session_id();
	client.device_id_ = _login.has_device_id() ? _login.device_id() : "";
	client.device_type_ = _login.has_device_type() ? _login.device_type() : DeviceType_UNKNOWN;
	client.password_ = _login.has_pwd() ? _login.pwd() : "";
	client.online_time = time(0);
    
    {
        std::lock_guard<std::recursive_mutex> lock_1(socket_userid_mutex_);
        socket_userid_[_sockfd] = _login.user_id();
    }
    {
        std::lock_guard<std::recursive_mutex> lock_1(user_map_mutex_);
        user_map_[_login.user_id()] = client;
    }
	RegistUserToRoute(client);

    return ERRNO_CODE_OK;
}

void ConnectionServer::ResendFailedPack(int _sockfd, UserId_t _userid) {
    //LOGD("消费未处理消息");

    std::list<PDUBase> temp_list;
    {
        std::lock_guard<std::recursive_mutex> lock_1(send_failed_cache_queue_mutex_);
        auto it = send_failed_cache_queue_.find(_userid);
        if (it == send_failed_cache_queue_.end()) {
            return;
        }
        temp_list = it->second;
        send_failed_cache_queue_.erase(_userid);
        LOGD("有%d条发送失败的消息正在重新发送", temp_list.size());
    }
    for(auto item = temp_list.begin(); item != temp_list.end(); item++) {
        Send(_sockfd, *item);
    }
}

void ConnectionServer::ConsumeHistoryMessage(UserId_t _userid) {

	ClientObject client;
	if (!find_client_by_userid(_userid, client)) {
		LOGE("not find user_id(%d)", _userid);
		return;
	}
    std::shared_ptr<char> base64_data(new char[4096], carray_deleter);
    PDUBase base;
	std::list<std::string> encode_imlist;
	//msg type: offline chat msg  and user timeout recv msg
	if (redis_client.GetOfflineIMList(_userid, encode_imlist)) {

		LOGD("user_id(%d) have %d offline chatmsg", _userid, encode_imlist.size());

		for (auto item = encode_imlist.begin(); item != encode_imlist.end(); item++) {
			if (item->length() > 3000) continue;

			memset(base64_data.get(), 0, 4096);
			int base64_size = base64_decode(base64_data.get(), item->c_str(), item->length());
			if (base64_size > 0 && OnPduParse(base64_data.get(), base64_size, base) > 0) {
				//	ProcessIMChat_fromRoute(base);
				/*IMChat_Personal_Notify im_notify;
				if (!im_notify.ParseFromArray(base.body.get(), base.length)) {
				LOGERROR(base.command_id, base.seq_id, "ConsumeHistoryMessage包解析错误");
				return;
				}
				const IMChat_Personal& im = im_notify.imchat();*/

				if (client.version == VERSION_0) {
					if (!Send(client.sockfd_, base)) {
						OnSendFailed(base);
					}
				}
				else {
					uint64_t msg_id = 0;
					if (base.command_id == IMCHAT_PERSONAL || base.command_id == IMCHAT_PERSONAL_NOTIFY) {
						IMChat_Personal_Notify im_notify;
						if (!im_notify.ParseFromArray(base.body.get(), base.length)) {
							LOGERROR(base.command_id, base.seq_id, "ConsumeHistoryMessage包解析错误");
							return;
						}
						const IMChat_Personal& im = im_notify.imchat();
						msg_id = im.msg_id();
						LOGD("offline msg_id(%ld):%s to user_id(%d)", msg_id, im.body().c_str(), _userid);
					}
					
					need_send_msg(_userid, client.sockfd_, base, msg_id);

				}
			}
		}

	}
	std::list<std::string> channel_list;
	if (redis_client.GetBroadcastOfflineIMList(_userid, channel_list)) {
		LOGD("user_id(%d) have %d broadcast msg ", _userid, channel_list.size());
		for (auto it = channel_list.begin(); it != channel_list.end(); ++it) {
			LOGD("channel:%s", (*it).c_str());
			std::string chat_msg = redis_client.GetBroadcastMsg(*it);
			if (chat_msg == "") {
				LOGD("not find channel:%s msg", (*it).c_str());
				return;
			}
			std::string tmp_msg_id = (*it).substr((*it).find(":") + 1);
			uint32_t msg_id = atoi(tmp_msg_id.c_str());
			Json::Reader reader;
			Json::Value root;
			if (!reader.parse(chat_msg, root)) {
				LOGE(" parse fail");
				return;
			}
			int cmd = atoi(root["CommandId"].asString().c_str());
			::google::protobuf::Message* notify;
			IMChat_Personal im;
			IMChat_Personal_Notify im_notify;
			Bulletin_Notify  bulletin_notify;
			if (cmd == IMCHAT_PERSONAL) {
				notify = &im_notify;
				im.set_msg_id(msg_id);
				im.set_src_usr_id(atoi(root["SvcUsrId"].asString().c_str()));
				im.set_src_phone(root["SvcPhone"].asString());
				im.set_content_type(atoi(root["ContentType"].asString().c_str()));
				im.set_command_id(atoi(root["CommandId"].asString().c_str()));
				im.set_body(root["Body"].asString());
				im.set_target_user_type(USER_TYPE_PERSONAL);
				im.set_timestamp(root["Timestamp"].asInt());
				LOGD("broadcast offline chat msg[ msg_id:%s] to user_id:%d", (*it).c_str(), _userid);
				ClientObject client;
				if (!find_client_by_userid(_userid, client)) {
					return;
				}

				im.set_target_user_id(_userid);
				im.set_target_phone(client.phone_);
				im_notify.set_allocated_imchat(&im);
			}
			else if (cmd == BULLETIN) {
				LOGD("broadcast offline bulletin msg [ msg_id:%s ] to user_id:%d", (*it).c_str(), _userid);
				Bulletin bul;
				notify = &bulletin_notify;
				bul.set_bulletin_id(msg_id);
				bul.set_content(root["Body"].asString());
				bul.set_publish_time(root["Timestamp"].asInt());
				bul.set_publisher_id(atoi(root["SvcUsrId"].asString().c_str()));
				bul.set_bulletin_type((Bulletin_Type)atoi(root["ContentType"].asString().c_str()));
				bul.set_publisher_phone(root["SvcPhone"].asString());
				auto it = bulletin_notify.add_bulletins();
				it->CopyFrom(bul);
			}
			PDUBase _pack;
			std::shared_ptr<char> body(new char[notify->ByteSize()], carray_deleter);
			notify->SerializeToArray(body.get(), notify->ByteSize());
			_pack.terminal_token = _userid;
			_pack.body = body;
			_pack.length = notify->ByteSize();

			if (cmd == BULLETIN) {
				_pack.command_id = BULLETIN_NOTIFY;
			}
			else {
				_pack.command_id = atoi(root["CommandId"].asString().c_str());
			}
			if (client.version == VERSION_0 || client.version==VERSION_1 && cmd == BULLETIN) {
				if (!Send(client.sockfd_, _pack)) {
					OnSendFailed(_pack);
				}
			}
			else {
				need_send_msg(_userid, client.sockfd_, _pack, msg_id);
			}

			if (cmd == IMCHAT_PERSONAL) {
				im_notify.release_imchat();
			}
		}
	}
}


bool ConnectionServer::find_userid_by_sockfd(int _sockfd, int &_userid, bool is_del) {
    std::lock_guard<std::recursive_mutex> lock_1(socket_userid_mutex_);
    auto it = socket_userid_.find(_sockfd);
    if (it != socket_userid_.end()) {
        _userid = it->second;
        if (is_del) {
            socket_userid_.erase(it);
        }
        return true;
    }
    return false;
}

bool ConnectionServer::find_client_by_userid(int _userid, ClientObject &_client, bool is_set_offline) {
    std::lock_guard<std::recursive_mutex> lock_1(user_map_mutex_);
    auto it = user_map_.find(_userid);
    if (it != user_map_.end()) {
        _client = it->second;
        if (is_set_offline) {
			it->second.online_status_ = OnlineStatus_Offline;
        }
        return true;
    }
    return false;
}

bool ConnectionServer::find_client_by_userid(int _userid, ClientObject * &_client, bool is_set_offline)
{
	std::lock_guard<std::recursive_mutex> lock_1(user_map_mutex_);
	auto it = user_map_.find(_userid);
	if (it != user_map_.end()) {
		_client = &it->second;
		if (is_set_offline) {
			it->second.online_status_ = OnlineStatus_Offline;
		}
		return true;
	}
	return false;
}

void ConnectionServer::set_user_state(int _userid, OnlineStatus state)
{
	std::lock_guard<std::recursive_mutex> lock_1(user_map_mutex_);
	auto it = user_map_.find(_userid);
	if (it != user_map_.end()) {
		it->second.online_status_ = state;
	}
	
}

int ConnectionServer::get_user_state(int _userid)
{
	std::lock_guard<std::recursive_mutex> lock_1(user_map_mutex_);
	auto it = user_map_.find(_userid);
	if (it != user_map_.end()) {
		return it->second.online_status_;
	}
	return OnlineStatus_Offline;
}

uint64_t ConnectionServer::getMsgId()
{
	uint64_t msg_id;
	std::lock_guard<std::recursive_mutex> lock1(m_msgid_mutex_);
	
	time_t now = time(NULL);
	msg_id = (uint64_t)now << 32 | (uint64_t)m_node_id_ << 24;
	if (now != m_lastTime) {
		m_cur_index_ = 0;
		m_lastTime = now;
	}
	++m_cur_index_;
	msg_id |= (m_cur_index_ & 0xffffff);//m_count shoud small 256*256*256;

	return msg_id;
}

bool ConnectionServer::need_send_msg(int _userid, int _sockfd, PDUBase& _base, uint64_t msg_id)
{
	ClientObject* target;
	if (!find_client_by_userid(_userid, target)) {
        LOGE("not find user_id(%d),send fail",_userid);
		return false;
	}

	Ackmsg* ackmsg = new Ackmsg(msg_id, _base);
	{
		//std::lock_guard<std::recursive_mutex> lock1(m_send_msg_mutex_);
	//	CAutoRWLock lock(&m_rwlock_, 'w');
		//if user send buffer_list not empty ,dont send again
		auto it = m_send_msg_map_.find(_userid);
		if (it != m_send_msg_map_.end() && !it->second.empty()) {
			it->second.push_back(ackmsg);//not send
			if (target->send_pending == 1) {
				return false;
			}
			
		}
		else {
			if (it == m_send_msg_map_.end()) {
				std::list<Ackmsg*> ml;
				ml.push_back(ackmsg);
				m_send_msg_map_.insert(std::pair<int, std::list<Ackmsg*>>(_userid, ml));

			}
			else if (it->second.empty()) {
				it->second.push_back(ackmsg);
				

			}
		}
	}
	target->send_pending = 1;
	target->send_time = get_mstime();
	Send(_sockfd, _base);
	record_waiting_ackmsg(_userid, msg_id);//msg_id not use;
	return true;
}

void ConnectionServer::record_waiting_ackmsg(int _userid,  uint64_t msg_id) {
	//std::lock_guard<std::recursive_mutex> lock(m_ack_msg_mutex_);
	m_ack_msg_map_.push(_userid, time(0));
}

void ConnectionServer::delete_ack_msg(int _userid, uint64_t msg_id)
{
	std::lock_guard<std::recursive_mutex> lock(m_ack_msg_mutex_);
	auto it = m_ack_msg_map_.find(_userid);
	if (it != m_ack_msg_map_.end()) {
		m_ack_msg_map_.erase(it);
	}
    else{
        LOGE("not find user_id(%d)",_userid);
    }
}

void ConnectionServer::check_send_msg()
{
	time_t now = time(NULL);
	std::vector<int> offline_users;
	{
		std::lock_guard<std::recursive_mutex> lock(m_ack_msg_mutex_);
		while (!m_ack_msg_map_.empty()) {
			auto it = m_ack_msg_map_.back();
			if (it.second + MSG_ACK_TIME < now) {//timeout recv ack
				offline_users.push_back(it.first);
				m_ack_msg_map_.pop();
				LOGE("timeout recv ack user_id(%d),prev(%d),now(%d)", it.first,it.second,now);
                continue;
			}
            break;
		}
	}
		//std::lock_guard<std::recursive_mutex> lock1(m_send_msg_mutex_);
	//CAutoRWLock lock(&m_rwlock_, 'w');
	for (auto it = offline_users.begin(); it != offline_users.end();it++ ) {
		/*auto mit = m_send_msg_map_.find(*it);
		if (mit != m_send_msg_map_.end()) {
			std::list<Ackmsg*>& msg_list = mit->second;
			LOGD("user_id(%d) offline msg num:%d",*it, msg_list.size());
			for (auto msg_it = msg_list.begin(); msg_it != msg_list.end();msg_it++ ) {
				SaveOfflineMsg(*it, (*msg_it)->pdu);//save timeout user msg
				delete *msg_it;
				
			}
			set_user_state(*it, OnlineStatus_Offline);
			//remove user send msg list;
			m_send_msg_map_.erase(*it);
		}*/
		PDUBase* pdu = new PDUBase;
		pdu->command_id = 0;
		pdu->terminal_token = *it;
		m_chat_msg_process.addJob(0, pdu,1);

	}
}

void ConnectionServer::ack_timeout_handler(int user_id)
{
	LOGD("handler ack timeout");
	ClientObject* object;
	if (find_client_by_userid(user_id, object)) {
		int cur_time = time(0);
		//if recv ack inner MSG_ACK_TIME return
		 if (get_mstime() - object->ack_time < MSG_ACK_TIME*1000 ) {
            LOGD("recv ack inner timeout"); 
			return;
		}
		 else if (cur_time - object->online_time < MSG_ACK_TIME && object->send_pending==1) {// user online ,but not recv ack send again
			 auto it = m_send_msg_map_.find(user_id);
			 if (it != m_send_msg_map_.end()) {//last msg is waiting for ack
				 if (!it->second.empty()) {
					
					Ackmsg* ackmsg = it->second.front();
					 LOGD("try again send msg_id(%ld)", ackmsg->msg_id);
					 Send(object->sockfd_, ackmsg->pdu);//ignore send result,if fail 
					 object->send_pending = 1;
					 record_waiting_ackmsg(user_id, ackmsg->msg_id);
					 
				 }
			 }
		 }
		 else {//save offline msg
			 char* data;
			 auto it = m_send_msg_map_.find(user_id);
			 if (it != m_send_msg_map_.end()) {//last msg is waiting for ack
				
				 std::list<Ackmsg*>& msg_list = it->second;
				 for (auto msg_it = msg_list.begin(); msg_it != msg_list.end(); msg_it++) {
					 int len = OnPduPack((*msg_it)->pdu, data);
					 if (len > 0) {
						 m_offline_works->enqueue(&ConnectionServer::SaveOfflineMsg_, this, user_id, data, len);
					 }
					 //	SaveOfflineMsg(*it, (*msg_it)->pdu);//save timeout user msg
					 delete *msg_it;

				 }
                 m_send_msg_map_.erase(it);
			 }
			 LOGD("timeout recv ack user_id(%d),set status offline",user_id);
             set_user_state(user_id, OnlineStatus_Offline);
		 }
	}
	else {
		LOGE("not find user_id(%d)", user_id);
	}

}

void ConnectionServer::storage_common_msg(SaveIMObject * object)
{
	std::string  tmp = object->ToJson();
	redis_client.InsertIMtoRedis(tmp);
	delete object;
}

void ConnectionServer::storage_apple_msg(std::string * object)
{
	redis_client.InsertIMPushtoRedis(*object);
	delete object;
}

void ConnectionServer::storage_broadoffline_msg(std::string user, std::string msg_id)
{
	redis_client.InsertBroadcastOfflineIMtoRedis(user, msg_id);
}


/*
 * 小的封装函数，只组合IM消息结果的包，发送给用户．
 */
void ConnectionServer::ReplyChatResult(int _sockfd, PDUBase &_pack, ERRNO_CODE _code, bool is_target_online, uint64_t msg_id) {
    IMChat_Personal_Ack im_ack;
    im_ack.set_errer_no(_code);
    im_ack.set_msg_id(msg_id);
    im_ack.set_is_target_online(is_target_online);
    std::shared_ptr<char> new_body(new char[im_ack.ByteSize()], carray_deleter);
    im_ack.SerializeToArray(new_body.get(), im_ack.ByteSize());
    _pack.body = new_body;
    _pack.length = im_ack.ByteSize();
    _pack.command_id = IMCHAT_PERSONAL_ACK;
    Send(_sockfd, _pack);
}

int ConnectionServer::PhoneQueryUserId(std::string _phone, int &_userid) {
	{
		//query from local cache
		std::lock_guard<std::recursive_mutex> lock_1(phone_userid_mutex_);
		Phone_userid_t::iterator it = phone_userid_.find(_phone);
		if (it != phone_userid_.end()) {
			_userid = it->second;
			return ERRNO_CODE_OK;
		}
	}
    if (!redis_client.IsHuxinUser(_phone)) {
        LOGE("IM消息处理, 目标用户%s为非呼信用户", _phone.c_str());
        return ERRNO_CODE_NOT_HUXIN_USER;
    }
    std::string userid = redis_client.GetUserId(_phone);
    if (userid == "") {
        LOGE("查询用户%s userid出错或者userid为空", _phone.c_str());
        return ERRNO_CODE_DB_SERVER_EXCEPTION;
    }
    _userid = std::atoi(userid.c_str());
	std::lock_guard<std::recursive_mutex> lock_1(phone_userid_mutex_);
	phone_userid_[_phone] = _userid;
    return ERRNO_CODE_OK;
}

int ConnectionServer::UserIdQueryPhone(int _userid, std::string &_phone) {
    Route_PhoneCheck phonecheck;
    Route_PhoneCheck_Ack phonecheck_ack;
    PDUBase base;

   // std::lock_guard<std::recursive_mutex> lock_1(block_tcp_client_mutex_);
    phonecheck.set_user_id(_userid);
	BlockTcpClient* block_tcp_client_ = getBlockTcpClient();
    if (!block_tcp_client_->SendProto(phonecheck, ROUTE_PHONE_CHECK, 0)) {
        block_tcp_client_->Read(base);
        if (!phonecheck_ack.ParseFromArray(base.body.get(), base.length)) {
            return 0;
        }
        _phone = phonecheck_ack.phone();
        return 1;
    }

    return 0;
}

/*
* 向路由注册用户
*/
void ConnectionServer::RegistUserToRoute(ClientObject &_client) {
	//LOGD("向路由注册用户");

	User_Login login;
	login.set_device_id(_client.device_id_);
	login.set_user_id(_client.userid_);
	login.set_phone(_client.phone_);
	login.set_device_type(_client.device_type_);
	login.set_pwd(_client.password_);
	login.set_session_id(_client.session_id_);

	PDUBase pack;
	pack.terminal_token = _client.userid_;
	ResetPackBody(pack, login, USER_LOGIN);
	route_client_.Send(pack);
}

bool ConnectionServer::IsUserOnline(UserId_t _userid) {
    Route_OnlineCheck online;
    Route_OnlineCheck_Ack online_ack;
    PDUBase base;

  //  std::lock_guard<std::recursive_mutex> lock_1(block_tcp_client_mutex_);
	BlockTcpClient* block_tcp_client_ = getBlockTcpClient();
    online.set_user_id(_userid);
    if (!block_tcp_client_->SendProto(online, ROUTE_ONLINECHECK, 0)) {
        block_tcp_client_->Read(base);
        if (!online_ack.ParseFromArray(base.body.get(), base.length)) {
            return false;
        }
        return online_ack.is_online();
    }

    return false;
}

void ConnectionServer::LoginUtil(Socketfd_t _sockfd, UserId_t _userid) {
  
    ClientObject client_object;
    client_object.sockfd_ = _sockfd;
    client_object.userid_ = _userid;
    client_object.device_id_ = "";
    client_object.device_type_ = DeviceType_UNKNOWN;
    client_object.online_status_ = OnlineStatus_Connect;
	client_object.online_time = time(0);
    std::string phone = "";
   
    if (!UserIdQueryPhone(_userid, phone)) {
        LOGE("query userid:%d phone fail",_userid);
        return;
    }

    if (phone == "") {
        LOGE("user_id:%d phone is null",_userid);
        return;
    }

    client_object.phone_ = phone;

    {
        std::lock_guard<std::recursive_mutex> lock_1(user_map_mutex_);
        user_map_[_userid] = client_object;
    }
    {
        std::lock_guard<std::recursive_mutex> lock_1(socket_userid_mutex_);
        socket_userid_[_sockfd] = _userid;
    }

    ++onliners_;
	RegistUserToRoute(client_object);

	LOGD("----------------------->user[user_id:%d,phone:%s,fd:%d] offline reconnect and login--------loginers[%d]-user_map[%d]--------->", _userid, phone.c_str(), _sockfd,(int)onliners_,user_map_.size());
}

void ConnectionServer::ServiceInfo() {
    int user_map_size = 0;
    int socket_userid_size = 0;
    {
        std::lock_guard<std::recursive_mutex> lock_1(user_map_mutex_);
        user_map_size = user_map_.size();
    }
    {
        std::lock_guard<std::recursive_mutex> lock_1(socket_userid_mutex_);
        socket_userid_size = socket_userid_.size();
    }
    printf("服务器信息\n");
    printf("IP:%s 端口:%d\n", ip_.c_str(), port_);
    printf("连接的用户数在线与不在线:%d\n", user_map_size);
    printf("socket连接数:%d\n", socket_userid_size);
}

BlockTcpClient * ConnectionServer::getBlockTcpClient()
{
	
	pthread_t  id = pthread_self();
	std::lock_guard<std::recursive_mutex> lock_1(block_tcp_client_mutex_);
	std::map<pthread_t, BlockTcpClient*>::iterator it = m_blockTcpClient.find(id);
	if (it != m_blockTcpClient.end()) {
		return it->second;
	}
	else {
		BlockTcpClient* client = new BlockTcpClient;
		client->Connect(route_ip_.c_str(), route_port_, false);
		m_blockTcpClient.insert(std::pair<pthread_t, BlockTcpClient*>(id, client));
		return client;
	}
	return NULL;
}

int ConnectionServer::getOnliners()
{
	return onliners_;
}

void ConnectionServer::reportOnliners()
{
	static uint64_t m_cronloops = 0;
	static uint64_t last_statistics_pkts = 0;
    loadbalance_client_.ReportOnliners(onliners_);
	run_with_period(24 * 3600 * 1000) {
		LOGD("statistics: [total recv msg:%ld,every day:%ld]", (uint64_t)m_total_recv_pkt, (uint64_t)m_total_recv_pkt - last_statistics_pkts);
		last_statistics_pkts = m_total_recv_pkt;
	}
	++m_cronloops;
	
}
