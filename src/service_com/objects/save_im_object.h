#ifndef _SAVE_IM_OBJECT_H
#define _SAVE_IM_OBJECT_H

#include <string>

class SaveIMObject
{
public:
    SaveIMObject();
    std::string ToJson();
	uint64_t id_;
    int pid_;
    int brand_;
    int content_type_;
    int delete_flag_;
    int reply_flag_;
    int collect_flag_;
    int sender_userid_;
    int recver_userid_;

    std::string sender_phone_;
    std::string recver_phone_;
    std::string body_;
    std::string add_time_;
};

#endif
