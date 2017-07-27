#ifndef _LOADBALANCE_OBJECT_H
#define _LOADBALANCE_OBJECT_H

#include <string>
#include "../../tools/typedef.h"
#define     ROUTE_STATE_OK               0
#define     ROUTE_STATE_ERR              1
class LoadbalanceObject {
public:
    LoadbalanceObject();

    // 当前负载的ip和端口
    std::string ip_;
    int port_;

    // 当前负载的用户数量
    int current_balance_user_num_;

    // 当前负载的注册时间
    int regist_timestamp_;

    Socketfd_t sockfd_;

	int state;
    bool operator > (const LoadbalanceObject &_lbo) {
        return current_balance_user_num_ > _lbo.current_balance_user_num_;
    }

    bool operator < (const LoadbalanceObject &_lbo) {
        return current_balance_user_num_ < _lbo.current_balance_user_num_;
    }

    bool operator()(const LoadbalanceObject &lhs, const LoadbalanceObject& rhs) const {
        return lhs.current_balance_user_num_ < rhs.current_balance_user_num_;
    }

    bool operator == (const LoadbalanceObject &_lbo) const {
        return _lbo.sockfd_ == sockfd_;
    }

};


#endif
