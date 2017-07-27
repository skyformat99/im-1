#ifndef _LOADBALANCE_REQUEST_H
#define _LOADBALANCE_REQUEST_H

#include <string>
#include "mongoose.h"
#include "route_server.h"

class LoadbalanceRequest {
public:
    LoadbalanceRequest() {}

    std::string s_http_port_;
    std::string s_http_uri_;

    struct mg_serve_http_opts s_http_serve_opts_;

    RouteServer *route_server_;

    void StartRestfulService(int _port, std::string _uri, RouteServer *_route_server);
    void RestfulService();
};

#endif
