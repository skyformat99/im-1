#include "main.h"
#include <iostream>
#include "route_server.h"
#include "loadbalance_request.h"
#include "config_file_reader.h"
#include <log_util.h>
#include <signal.h>
#include <errno.h>
#include <cstdio>
RouteServer route_server;
RedisClient redis_client;
LoadbalanceRequest balance_request;
ThreadPool thread_pool(CONNECTPOOL_AND_THREADPOOL_NUMBER);

int total_recv_pkt=0;
int sec_recv_pkt=0;
int send_success_pkt=0;
int total_login=0;
int online_check=0;
int send_success_pkts=0;
static char*   im_signal;

static int get_options(int argc, char *const *argv)
{
	u_char     *p;
	int   i;

	for (i = 1; i < argc; i++) {

		p = (u_char *)argv[i];

		if (*p++ != '-') {
			fprintf(stderr, "invalid option: \"%s\"\n", argv[i]);
			return -1;
		}

		while (*p) {

			switch (*p++) {

			case 's':
				if (*p) {
					im_signal = (char *)p;

				}
				else if (argv[++i]) {
					im_signal = argv[i];

				}
				else {
					fprintf(stderr, "option \"-s\" requires parameter\n");
					return -1;
				}

				if (strcmp(im_signal, "stop") == 0
					|| strcmp(im_signal, "reload") == 0 || strcmp(im_signal, "restart") == 0)
				{

					goto next;
				}

				fprintf(stderr, "invalid option: \"-s %s\"\n", im_signal);
				return -1;

			default:
				fprintf(stderr, "invalid option: \"%c\"\n", *(p - 1));
				return -1;
			}
		}

	next:

		continue;
	}

	return 0;
}

static int handler_signal(const char* pid_file) {
	int fd = open(pid_file, O_RDONLY);
	if (fd == -1) {
		perror("open");
		return -1;
	}
	pid_t  pid = 0;
	if (read(fd, &pid, sizeof(pid)) != sizeof(int)) {
		perror("read");
		return -1;
	}
	if (!strcmp(im_signal, "stop") || !strcmp(im_signal, "restart")) {
		if (kill(pid, SIGKILL) == -1) {
			fprintf(stderr, "close IMServer fail");
			return -1;
		}
	}
	close(fd);
	return 0;

}

int daemon() {
	int  fd;
	pid_t pid = fork();
	switch (pid) {
	case -1:
		fprintf(stderr, "fork() failed\n");
		return -1;

	case 0:
		break;

	default:
		exit(0);
	}

	if (setsid() == -1) {
		fprintf(stderr, "setsid() failed\n");
		return -1;
	}

	umask(0);

	fd = open("/dev/null", O_RDWR);
	if (fd == -1) {
		fprintf(stderr,
			"open(\"/dev/null\") failed\n");
		return -1;
	}

	if (dup2(fd, STDIN_FILENO) == -1) {
		fprintf(stderr, "dup2(STDIN) failed\n");
		return -1;
	}

	if (dup2(fd, STDOUT_FILENO) == -1) {
		fprintf(stderr, "dup2(STDOUT) failed\n");
		return -1;
	}

	if (fd > STDERR_FILENO) {
		if (close(fd) == -1) {
			fprintf(stderr, "close() failed\n");
			return -1;
		}
	}

	return 0;
}


void start_server() {
   
	if (route_server.init() == -1) {
		LOGE("route server start fail");
		return;
	}
	route_server.start();
}

void start_redis() {
    ConfigFileReader redis(CONF_PUBLIC_URL);
    // 初始化Redis连接池
    redis_client.Init_Pool(redis.ReadString(CONF_REDIS_IP), redis.ReadInt(CONF_REDIS_PORT),
                           redis.ReadString(CONF_REDIS_AUTH), CONNECTPOOL_AND_THREADPOOL_NUMBER);
    redis_client.SetKeysExpire(redis.ReadInt(CONF_REDIS_EXPIRE));
    route_server.InitIMUserList();
}
void count(){
	while(1){
	    sleep(1);
	    printf("online_check:%d, total recv %d pkt, recv %d pkt/sec,send success %d pkt\n", online_check,total_recv_pkt,total_recv_pkt-sec_recv_pkt,send_success_pkt);
	    sec_recv_pkt=total_recv_pkt;
        }
}

int main(int argc, char *argv[]) {
	initLog(CONF_LOG);
	ConfigFileReader reader(CONF_PUBLIC_URL);

	std::string value=reader.ReadString("daemon");
	if (value == "on") {
		if (daemon() == -1) {
			return 0;
		}
	}
	signal(SIGPIPE, SIG_IGN);

    // 读取redis用户列表
   start_redis();

	start_server();

    return 0;
}
