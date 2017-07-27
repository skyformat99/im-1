#include "loadbalance_object.h"

LoadbalanceObject::LoadbalanceObject() {
    current_balance_user_num_ = 0;
	state = ROUTE_STATE_OK;
}
