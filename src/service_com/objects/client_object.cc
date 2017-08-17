#include "client_object.h"

ClientObject::ClientObject() {
    device_type_ = DeviceType_UNKNOWN;
    version=0;
	online_time = 0;
	ack_time = 0;
	send_pending = 0;
}
