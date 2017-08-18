#ifndef _CLIENT_OBJECT_H
#define _CLIENT_OBJECT_H

#include <string>
#include "../../protobuf/YouMai.Basic.pb.h"
#include "../../tools/typedef.h"

using namespace com::proto::basic;

enum OnlineStatus {
	OnlineStatus_Offline = 0,
	OnlineStatus_Connect = 1,
	OnlineStatus_Logout = 2
};

class ClientObject {
public:
    ClientObject();

    std::string phone_;
    std::string session_id_;
    std::string password_;
    std::string device_id_;

    UserId_t userid_;

    Socketfd_t sockfd_;

    Device_Type device_type_;

    OnlineStatus online_status_;
	int          version;


	time_t          online_time; //online time;
	long long       ack_time;
	long long       send_time;
	int             send_pending;//send but recv ack num;
};

#endif
