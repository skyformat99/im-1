#include "save_im_object.h"
#include <json/json.h>

SaveIMObject::SaveIMObject() {

}

std::string SaveIMObject::ToJson() {

    Json::FastWriter fwriter;
    Json::Value root;
	root["id"] = id_;
    root["pid"] = pid_;
    root["brand"] = brand_;
    root["contentType"] = content_type_;
    root["delFlag"] = delete_flag_;
    root["replyFlag"] = reply_flag_;
    root["collectFlag"] = collect_flag_;
    root["senderUserid"] = sender_userid_;
    root["receiverUserid"] = recver_userid_;

    root["senderPhone"] = sender_phone_;
    root["receiverPhone"] = recver_phone_;
    root["body"] = body_;
    root["addtime"] = add_time_;

    return fwriter.write(root);
}
