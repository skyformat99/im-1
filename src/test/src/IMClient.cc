#include "IMClient.h"
#include "YouMai.Bulletin.pb.h"
#include <log_util.h>
#include <unistd.h>
#include <thread>
using namespace com::proto::bulletin;
extern std::vector<User*> g_recv_users;
extern IMClient client;
void count() {
	int sec_recv_pkt = 0;
	while (1) {
		sleep(1);
		int total = client.getRecvPkt();
		printf("total recv:%d,every recv:%d\n", total, total - sec_recv_pkt);
		sec_recv_pkt = total;
	}

}
IMClient::IMClient()
{
	m_num = 0;
	total_recv_pkts = 0;
	m_login = 0;
}

IMClient::~IMClient()
{
}

void IMClient::OnRecv(int _sockfd, PDUBase & _base)
{
	
}

void IMClient::OnRecv(int _sockfd, PDUBase * _base)
{
	switch (_base->command_id) {
	case IMCHAT_PERSONAL:
		chatMsg(_sockfd,*_base);
		break;
	case USER_LOGIN_ACK:
		loginAck(*_base);
		break;
    case BULLETIN_NOTIFY:
       printf("recv cmd:%d\n",_base->command_id);
        bulletin(_sockfd,*_base);
        break;
	}
	delete _base;
}

void IMClient::OnConn(int _sockfd)
{
}

void IMClient::OnDisconn(int _sockfd)
{
}

void IMClient::OnSendFailed(PDUBase & _data)
{
}


void IMClient::loginAck(PDUBase & _base) {
	User_Login_Ack login_ack;
	if (!login_ack.ParseFromArray(_base.body.get(), _base.length)) {
		LOGE("IMChat_Personal包解析错误");
		//	ReplyChatResult(_sockfd, _base, ERRNO_CODE_DATA_SRAL);
		return;
	}
	int res = login_ack.errer_no();
	if (res == ERRNO_CODE_OK) {
		m_login++;
		printf("login success\n");
		if (m_login == g_recv_users.size()) {
			printf("all user login success\n");
			sleep(1);
			printf("begin recv msg\n");

			getchar();
			thread c(::count);
			c.detach();
		}

	}
}

void IMClient::bulletin(int _sockfd,PDUBase & _base){
    Bulletin_Notify  notify;
   if (!notify.ParseFromArray(_base.body.get(), _base.length)) {
    LOGE("roadcast parse fail");
     return;

   }
   printf("bulletin\n");
   auto it=m_sock_userid.find(_sockfd);
	++total_recv_pkts;
    IMChat_Personal_recv_Ack ack;
    ack.set_msg_id(1);
    ack.set_user_id(it->second);
	PDUBase _pack;
    _pack.terminal_token=_base.terminal_token;
	_pack.command_id = IMCHAT_PERSONAL_ACK;
	std::shared_ptr<char> body(new char[ack.ByteSize()]);
	ack.SerializeToArray(body.get(), ack.ByteSize());
	_pack.body = body;
	_pack.length = ack.ByteSize();
	Send(_sockfd, _pack);
}
void IMClient::chatMsg(int _sockfd,PDUBase & _base)
{
	IMChat_Personal_Notify  notify;
	Device_Type device_type = DeviceType_UNKNOWN;

	if (!notify.ParseFromArray(_base.body.get(), _base.length)) {
      
	//O	LOGE("IMChat_Personal包解析错误");
		//	ReplyChatResult(_sockfd, _base, ERRNO_CODE_DATA_SRAL);
		return;
	}

   const IMChat_Personal& im=notify.imchat();

	if (!im.has_body()) {
        printf("not boyd\n");
	//	LOGERROR(_base.command_id, _base.seq_id, "ProcessIMChat_Personal, !has_body");
		//ReplyChatResult(_sockfd, _base, ERRNO_CODE_INVALID_IM_CHAT_EMPTY_BODY_NOT_ALLOWED);
		//return;
	}
   auto it=m_sock_userid.find(_sockfd);
    //printf("user_id:%d,body:%s\n",it->second,im.body().c_str());
	++total_recv_pkts;
    IMChat_Personal_recv_Ack ack;
    ack.set_msg_id(im.msg_id());
    ack.set_user_id(it->second);
	PDUBase _pack;
    _pack.terminal_token=it->second;
	_pack.command_id = IMCHAT_PERSONAL_ACK;
	std::shared_ptr<char> body(new char[ack.ByteSize()]);
	ack.SerializeToArray(body.get(), ack.ByteSize());
	_pack.body = body;
	_pack.length = ack.ByteSize();
	Send(_sockfd, _pack);
}

void IMClient::OnLogin(int _sockfd)
{
	if (m_num > g_recv_users.size()) {
		printf("login users too manay ");
			return;
	}
	User* user = g_recv_users[m_num];
	User_Login login;
	login.set_user_id(user->id);
	login.set_phone(user->msisdn);
	login.set_pwd(user->passwd);
	login.set_session_id(user->sessid);
    login.set_version(1);
	PDUBase _pack;
	_pack.command_id = USER_LOGIN;
	std::shared_ptr<char> body(new char[login.ByteSize()]);
	login.SerializeToArray(body.get(), login.ByteSize());
	_pack.body = body;
	_pack.length = login.ByteSize();
	Send(_sockfd, _pack);
	++m_num;
    m_sock_userid[_sockfd]=user->id;
	
}

int IMClient::getRecvPkt()
{
	return total_recv_pkts;
}

int IMClient::getLogin()
{

	return m_login;
}



