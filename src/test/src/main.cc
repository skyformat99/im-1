#include "IMClient.h"
#include <thread>
#include <string>
#include <redis_client.h>
#include <log_util.h>
#include <unistd.h>
#include <algorithm>
#include <iterator>
using namespace std;
#define IP   "192.168.17.130"
#define PORT  8003


RedisClient redis_client;
std::vector<User*> g_users;
std::vector<User*> g_send_users;
std::vector<User*> g_recv_users;
IMClient client;
void SyncUserData(std::list<std::string>& _keys) {
    int num=0;
	if (_keys.size() == 0) return;

	for (auto it = _keys.begin(); it != _keys.end(); it++) {
        if(num++==2200){
            break;
        }
		std::string phone = (*it).substr((*it).find(":") + 1);;
		std::string userid = redis_client.GetUserId(phone);
		if (userid != "") {
			//user_phone_[atoi(userid.c_str())] = phone;
			User* user = new User;
			user->id = atoi(userid.c_str());
            if(user->id==29845407){
                printf("phone:%s\n",phone.c_str());
                
            }
			user->msisdn = phone;
			user->name = "";
			user->passwd = "";
			user->sessid = redis_client.GetSessionId(phone);
			g_users.push_back(user);
			//LOGD("正在同步数据库，userid:%s, phone:%s", userid.c_str(), phone.c_str());
		}
	}
}


int main(int argc,char** argv) {
    if(argc<3){
        printf("please input ip and port\n");
        return 0;
    }
    char ip[]="192.168.0.14";
    char auth[]="ym1234";
    redis_client.Init_Pool(ip,6380,auth,1);
	std::list<std::string> keys;

	redis_client.GetIMUserList(keys);
	LOGI("total user:%d", keys.size());

	
	SyncUserData(keys);
	LOGD("sync success ,total users:%d", g_users.size());
	int pagesize;
    printf("pagesize:\n");
	scanf("%d", &pagesize);
	int pagenum;
    printf("start pagenum:\n");
	scanf("%d", &pagenum);
    printf("pagenum=%d\n",pagenum);
	copy(g_users.begin() + pagesize*(pagenum - 1), g_users.begin() + pagesize*pagenum, back_inserter(g_recv_users));
	printf("recv user:%d", g_recv_users.size());
	printf("begin connection....\n");
	sleep(1);
	
	client.StartServer(argv[1], atoi(argv[2]), pagesize);
}
