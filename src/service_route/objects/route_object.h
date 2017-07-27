#ifndef _ROUTE_OBJECT_H
#define _ROUTE_OBJECT_H

#include <string>
#include "../../protobuf/YouMai.Basic.pb.h"
#include "../../tools/typedef.h"

using namespace com::proto::basic;

enum OnlineStatus {
    OnlineStatus_Offline = 0,
    OnlineStatus_Connect = 1,
};

class RouteObject {
public:
    RouteObject();

    Socketfd_t sockfd_; // socket fd which com server connect to route
    UserId_t userid_;

    std::string phone_;
    std::string device_id_;

    OnlineStatus online_status_;
    Device_Type device_type_;

};

#endif
