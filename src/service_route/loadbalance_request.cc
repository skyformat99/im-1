#include "loadbalance_request.h"
#include "log_util.h"
#include <thread>

LoadbalanceRequest *obj;

/*
 * 具体restful业务逻辑处理函数．
 * 解析get参数，
 * 请求分配IP，并返回处理结果．
 */
void handle_sum_call(struct mg_connection *nc, struct http_message *hm) {
    char userid[50] = "";
    int userid_value = 0;

    /* Get form variables */
    mg_get_http_var(&hm->query_string, "userid", userid, sizeof(userid));

    /* Send headers */
    mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");

    userid_value = atoi(userid);
    if (userid_value == 0) {
        LOGE("请求的负载的userid=0");
        return;
    }

    std::string ip = "";
    int port = 0;
    //obj->route_server_->AllocateLoadbalance(ip, port, userid_value);
    //LOGD("handle_sum_call,负载信息%d, %s", port, ip.c_str());

    /* Compute the result and send it back as a JSON object */
    mg_printf_http_chunk(nc, "{ \"ip\": \"%s\",\"port\":%d }", ip.c_str(), port);
    mg_send_http_chunk(nc, "", 0); /* Send empty chunk, the end of response */
}

/*
 * http 事件处理的回调事件．
 */
void ev_handler(struct mg_connection *nc, int ev, void *ev_data) {
    struct http_message *hm = (struct http_message *) ev_data;
    switch (ev) {
    case MG_EV_HTTP_REQUEST:
        if (mg_vcmp(&hm->uri, obj->s_http_uri_.c_str()) == 0) {
            handle_sum_call(nc, hm); /* Handle RESTful call */
        }
        nc->flags |= MG_F_SEND_AND_CLOSE;
        //LOGD("MG_EV_HTTP_REQUEST balance_ptr->uri_.c_str()%s", obj->s_http_uri_.c_str());
        break;
    default:
      break;
  }
}

/*
 * 启动restful服务，此为线程．
 */
void LoadbalanceRequest::StartRestfulService(int _port, std::string _uri, RouteServer *_route_server) {
    s_http_port_ = std::to_string(_port);
    s_http_uri_ = _uri;
    route_server_ = _route_server;
    obj = this;

    std::thread run(&LoadbalanceRequest::RestfulService, this);
    run.detach();
}

void LoadbalanceRequest::RestfulService() {
    struct mg_mgr mgr;
    struct mg_connection *nc;
    struct mg_bind_opts bind_opts;
    char *cp;
    const char *err_str;

    mg_mgr_init(&mgr, NULL);

    /* Use current binary directory as document root */
    if ((cp = strrchr((char*)"route_server", DIRSEP)) != NULL) {
        *cp = '\0';
    }
    s_http_serve_opts_.document_root = "route_server";

    /* Set HTTP server options */
    memset(&bind_opts, 0, sizeof(bind_opts));
    bind_opts.error_string = &err_str;

    nc = mg_bind_opt(&mgr, s_http_port_.c_str(), &ev_handler, bind_opts);
    if (nc == NULL) {
      LOGE("Error starting RESTful server on port %s: %s", s_http_port_.c_str(), *bind_opts.error_string);
      exit(1);
    }

    mg_set_protocol_http_websocket(nc);
    s_http_serve_opts_.enable_directory_listing = "yes";

    /* For each new connection, execute ev_handler in a separate thread */

    LOGD("Starting RESTful server on port %s, serving %s", s_http_port_.c_str(), s_http_serve_opts_.document_root);
    for (;;) {
        mg_mgr_poll(&mgr, 1000);
    }
    mg_mgr_free(&mgr);
}
