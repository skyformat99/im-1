
#include <broadcast.h>
#include <log_util.h>
#include<unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "config_file_reader.h"

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

int main() {

	signal(SIGPIPE, SIG_IGN);
	initLog(CONF_LOG);
	daemon();
	BroadcastServer server;

	// 读取公共配置信息
	ConfigFileReader reader(CONF_PUBLIC_URL);

	if (server.init() == -1) {
		LOGE("httpserver init fail");
		return 0;
	}
	server.start();
	while (1) {
		sleep(1);
	}
}
