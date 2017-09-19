#include "tcp_server.h"
#include "log_util.h"
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include "connection.h"
#define MAX_ACCEPTS_PER_CALL  1000

extern int total_recv_pkt;
int  TcpServer::getConnectNum(){
    return m_conns.size();
}
void TcpServer::accept_cb(int fd, short mask, void* privdata) {
	TcpServer* pInstance = static_cast<TcpServer*>(privdata);
	if (pInstance) {
		pInstance->Accept(fd);
		pInstance->OnConn(fd);
	}
}

void TcpServer::write_cb(int fd, short mask, void* privdata)
{
	Connection* conn = static_cast<Connection*>(privdata);
	if (!conn) {
		return;
	}
	int ret = conn->write();
	if (IO_ERROR == ret) {
	
		LOGE("fd=%d send error", conn->fd);

	}
	else if (IO_CLOSED == ret) {
		
		LOGE("fd=%d close", conn->fd);
	}
	else {
		if (conn->empty()) {
            TcpServer* pInstance=static_cast<TcpServer*>(conn->pInstance);
            if(pInstance){
                pInstance->delEvent(conn->fd,SD_WRITABLE);
            }
		}
		return;
	}

	// write error
	/*aeDeleteFileEvent(loop,conn->fd, AE_READABLE | AE_WRITABLE);
	close(conn->fd);
    m_conns.remove(conn->fd);
	delete conn;*/
    TcpServer* pInstance=static_cast<TcpServer*>(conn->pInstance);
    if(pInstance){
		
        pInstance->OnDisconn(conn->fd);
    }
}

int TcpServer::StartClient(std::string _ip, short _port)
{
	
	struct sockaddr_in serveraddr;

	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		LOGE("create sockef fail");
		return -1;
	}

	bzero(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	const char *local_addr = _ip.c_str();
	if(inet_aton(local_addr, &(serveraddr.sin_addr))==0){
        LOGE("Invaild ip:%s",local_addr);
        return -1;
    }
	serveraddr.sin_port = htons(_port);
	if (connect(fd, (struct sockaddr*)&serveraddr, sizeof(serveraddr)) < 0){
		LOGE("connect ip(%s) port(%d) fail,%s", _ip.c_str(), _port, strerror(errno));
		return -1;
	}
	
	setnonblocking(fd);
	Connection* conn = new (std::nothrow)Connection;
	if (NULL == conn) {
		LOGE("alloc memory fail");
		return -1;
	}
	conn->fd = fd;
	conn->pInstance = this;
	std::lock_guard<std::recursive_mutex> lock_1(m_ev_mutex);
	 m_conns.insert(std::pair<int, Connection*>(fd, conn));
	
	loop->createFileEvent(fd, SD_READABLE, read_cb, conn);
    return fd;

}

long long TcpServer::CreateTimer(long long milliseconds, sdTimeProc * proc, void * clientData)
{
	return loop->createTimeEvent(milliseconds, proc, clientData);
}

void TcpServer:: delEvent(int fd,int mask){
     std::lock_guard<std::recursive_mutex> lock_1(m_ev_mutex);
	loop->deleteFileEvent( fd, mask);

}
void TcpServer::close_cb(int _sockfd) {
    /*
     * close the socket, we are done with it
     * poll_event_remove(poll_event, sockfd);
     */
    OnDisconn(_sockfd);
}

void TcpServer::parse(Connection* conn) {
	int fd = conn->fd;
	char* data = conn->buf;
	while (conn->buf_len >= HEAD_LEN) {
		//没有不完整的包
		if (!conn->less_pkt_len) {
			int* startflag=(int*)(data);
			if (ntohl(*(startflag)) == PDUBase::startflag)//正常情况下 先接收到包头
			{
				if (conn->buf_len >= HEAD_LEN) {
					int data_len = ntohl(*(int*)(data + 20));
					int pkt_len = data_len + HEAD_LEN;
					//	msg_t msg;
					PDUBase* pdu = new PDUBase;
					
					int len = conn->buf_len >= pkt_len ? pkt_len : conn->buf_len;
					_OnPduParse(data, len, *pdu);
					if (conn->buf_len >= pkt_len) {

						conn->buf_len -= pkt_len;
						conn->recv_pkt++;
						data += pkt_len;
                        if(pdu->data_type_=='u'){
                            delete pdu;
                         }
                        else{

						//	msg_info(msg);
						    OnRecv(fd, pdu);
                        }
					}
					//not enough a pkt ;
					else {
						conn->pdu = pdu;//记录不完整pdu
						conn->pkt_len = pkt_len;
						conn->less_pkt_len = pkt_len - conn->buf_len;
						conn->buf_len = 0;
						break;
					}
				}
			}
			else {//找到包头
                LOGW("err data");
				int i;
				for (i = 0; i < conn->buf_len - 3; ++i) {
					if (ntohl(*((int*)(data+i))) == PDUBase::startflag) {
						data += i;
						conn->buf_len -= i;
						break;
					}
				}
				if (i == conn->buf_len - 3) {
					conn->clear();
					break;
				}
			}
		}
		else {
			//处理不完整pdu
			int less_pkt_len = conn->less_pkt_len;
			PDUBase* pdu = conn->pdu;
			char* pData = pdu->body.get();
			int len = conn->buf_len >= less_pkt_len ? less_pkt_len : conn->buf_len;
			memcpy(pData + (conn->pkt_len - conn->less_pkt_len-HEAD_LEN), data, len);
			if (conn->buf_len >= less_pkt_len) {
				
				data += less_pkt_len;
				conn->buf_len -= less_pkt_len;
				
				//清空记录
				conn->pdu = NULL;
				conn->pkt_len = 0;
				conn->less_pkt_len = 0;
				conn->recv_pkt++;
				//msg_info(msg);
                if(pdu->data_type_=='u'){
                    delete pdu;
                }
                else{
				    OnRecv(fd, pdu);
                }
			}
			else {
				conn->less_pkt_len -= conn->buf_len;
				conn->buf_len = 0;
			}
		}
	}

	//move not enough header data to buf start
	if (conn->buf_len && data != conn->buf) {
		memmove(conn->buf, data, conn->buf_len);
	}
}

void TcpServer::read_cb(int fd, short mask, void* privdata) {
	Connection* conn = static_cast<Connection*>(privdata);
	if (!conn) {
		return;
	}
	int ret = conn->read();
	if (IO_ERROR == ret) {
	    LOGE("read err(%s),will drop the packet",strerror(errno));
	
	}
	else if (IO_CLOSED == ret) {
		LOGE( "client close fd(%d)",fd);
	}
	else {
		TcpServer* pInstance = static_cast<TcpServer*>(conn->pInstance);
		pInstance->parse(conn);
		return;
	}
	//TODO
    TcpServer* pInstance=static_cast<TcpServer*>(conn->pInstance);
    if(pInstance){
        pInstance->OnDisconn(conn->fd);
    }
    /*aeDeleteFileEvent(loop, conn->fd, AE_READABLE);
	close(conn->fd);
    m_conns.remove(conn->fd);
	delete conn;*/
}

TcpServer::TcpServer() {
    listen_num = 1;
    ip_ = "127.0.0.1";
	loop = getEventLoop();
	loop->init(150000);
}

TcpServer::~TcpServer() {
    close(listen_sockfd_);
}

void TcpServer::StartServer(std::string _ip, short _port) {
    ip_ = _ip;
    port_ = _port;

	struct sockaddr_in serveraddr;

    listen_sockfd_ = socket(AF_INET, SOCK_STREAM, 0);

    setreuse(listen_sockfd_);

    bzero(&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    const char *local_addr =  _ip.c_str();
    inet_aton(local_addr, &(serveraddr.sin_addr));
    serveraddr.sin_port = htons(_port);

	bindsocket(listen_sockfd_, local_addr, _port);
	listensocket(listen_sockfd_, SOMAXCONN);
	setnonblocking(listen_sockfd_);
	loop->createFileEvent(listen_sockfd_, SD_READABLE, accept_cb, this);

    PollStart();
}

void TcpServer::PollStart() {

    
	loop->main();
}

void TcpServer::CloseFd(int _sockfd) {
	std::lock_guard<std::recursive_mutex> lock_1(m_ev_mutex);
    loop->deleteFileEvent(_sockfd, SD_READABLE | SD_WRITABLE);
    std::map<int,Connection*>::iterator it=m_conns.find(_sockfd);
    if(it!=m_conns.end()){
        delete it->second;
        m_conns.erase(it);
    }
    close(_sockfd);

    //LOGD("关闭sockfd:%d", _sockfd);
}

void TcpServer::Accept(int listener)
{
	struct sockaddr_in ss;
#ifdef WIN32
	int slen = sizeof(ss);
#else
	socklen_t slen = sizeof(ss);
#endif
    int fd;
    int max=MAX_ACCEPTS_PER_CALL;
    while(max--){
	    fd = ::accept(listener, (struct sockaddr*)&ss, &slen);
	    if (fd <= 0)
    	{
            if(errno==EINTR){
                continue;
            }
            else if(errno!=EAGAIN){

	    	    LOGE("accept fail:%s",strerror(errno));
            }
            return;
	    }
		
        LOGD("accept new connection [fd:%d,addr:%s:%d]",fd,inet_ntoa(ss.sin_addr),ntohs(ss.sin_port));
		setnonblocking(fd);
		anetKeepAlive(fd,6);
		{
			Connection* conn = new (std::nothrow)Connection;
			if (NULL == conn) {
				LOGE("alloc memory fail");
				return;
			}
			conn->fd=fd;
			conn->pInstance = this;
			std::lock_guard<std::recursive_mutex> lock_1(m_ev_mutex);
			auto res=m_conns.insert(std::pair<int, Connection*>(fd, conn));
            if(!res.second){
                LOGE("insert fail");
            }
			loop->createFileEvent( fd, SD_READABLE, read_cb, conn);

		}
    }
		//LOGW("fd=%d connect", fd);
	
}

bool TcpServer::Send(int _sockfd, PDUBase &_data) {
    std::shared_ptr<char> sp_buf;

	msg_t msg;
    int len = OnPduPack(_data, msg.m_data);
    if (len > 0) {
		msg.m_alloc = len;
		msg.m_len = len;
       return Send(_sockfd, msg, len);
       
    }
    return len > 0;
}

bool TcpServer::Send(int _sockfd, const char *_buffer, int _length) {
	if (_length > 0) {
		msg_t msg;
		if (bufalloc(msg, _length)) {
			memcpy(msg.m_data, _buffer, _length);
            msg.m_len=_length;
            std::lock_guard<std::recursive_mutex> lock_1(m_ev_mutex);
			std::map<int, Connection*>::iterator it = m_conns.find(_sockfd);
			if (it != m_conns.end()) {
				it->second->push(msg);
				loop->createFileEvent( _sockfd, SD_WRITABLE, write_cb, it->second);
			}
			else {
				LOGE("not find socket");
				buffree(msg);
			}
			
		}
	}
    return true;
}

bool TcpServer::Send(int _sockfd,  msg_t _msg, int _length) {
	if (_length > 0) {

		std::lock_guard<std::recursive_mutex> lock_1(m_ev_mutex);
		std::map<int, Connection*>::iterator it = m_conns.find(_sockfd);
		if (it != m_conns.end()) {
			it->second->push(_msg);
			loop->createFileEvent(_sockfd, SD_WRITABLE, write_cb, it->second);
		}
		else {
			LOGE("not find socket");
			buffree(_msg);
			return false;
		}
	}
	return true;
}

void TcpServer::setlinger(int _sockfd) {
    struct linger ling;
    ling.l_onoff = 0;
    ling.l_linger = 0;
    setsockopt(_sockfd, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling));
}

void TcpServer::setreuse(int _sockfd) {
    int opt = 1;
    setsockopt(_sockfd ,SOL_SOCKET,SO_REUSEADDR,(char *)&opt,sizeof(opt));
}

void TcpServer::setnodelay(int _sockfd) {
    int enable = 1;
    setsockopt(_sockfd, IPPROTO_TCP, TCP_NODELAY, (void*)&enable, sizeof(enable));

    int keepalive = 1;
    setsockopt(_sockfd, SOL_SOCKET, SO_KEEPALIVE, (void*)&keepalive, sizeof(keepalive));
    int keepcnt = 5;
    int keepidle = 30;
    int keepintvl = 1000;

    setsockopt(_sockfd, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(int));
    setsockopt(_sockfd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(int));
    setsockopt(_sockfd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(int));
}

int TcpServer::anetKeepAlive(int fd, int interval)
{
	int val = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val)) == -1)
	{
		LOGE("setsockopt SO_KEEPALIVE: %s", strerror(errno));
		return -1;
	}

	/* Default settings are more or less garbage, with the keepalive time
	* set to 7200 by default on Linux. Modify settings to make the feature
	* actually useful. */

	/* Send first probe after interval. */
	val = interval;
	if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &val, sizeof(val)) < 0) {
		LOGE("setsockopt TCP_KEEPIDLE: %s\n", strerror(errno));
		return -1;
	}

	/* Send next probes after the specified interval. Note that we set the
	* delay as interval / 3, as we send three probes before detecting
	* an error (see the next setsockopt call). */
	val = interval / 3;
	if (val == 0) val = 1;
	if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &val, sizeof(val)) < 0) {
		LOGE("setsockopt TCP_KEEPINTVL: %s\n", strerror(errno));
		return -1;
	}

	/* Consider the socket in error state after three we send three ACK
	* probes without getting a reply. */
	val = 3;
	if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &val, sizeof(val)) < 0) {
		LOGE("setsockopt TCP_KEEPCNT: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

void TcpServer::setnonblocking(int _sockfd) {
    int opts;
    opts = fcntl(_sockfd, F_GETFL);
    if(opts < 0) {
        perror("fcntl(sock, GETFL)");
        exit(1);
    }
    opts = opts | O_NONBLOCK;
    if(fcntl(_sockfd, F_SETFL, opts) < 0) {
        perror("fcntl(sock, SETFL, opts)");
        exit(1);
    }
}

void TcpServer::bindsocket(int _sockfd, const char *_pAddr, int _port) {
    struct sockaddr_in sock_addr;
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_addr.s_addr = inet_addr(_pAddr);
    sock_addr.sin_port = htons(_port);

    if (bind(_sockfd, (struct sockaddr *)&sock_addr, sizeof(sock_addr)) < 0) {
        perror("socket bind failed.");
        exit(-1);
    }
}

void TcpServer::listensocket(int _sockfd, int _conn_num) {
    if (listen(_sockfd, _conn_num) < 0) {
        perror("socket listen failed");
        exit(-2);
    }
}
