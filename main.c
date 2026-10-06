#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define __USE_GNU
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 8080
#define POOL_COUNT 3
#define LISTEN_BACKLOG 50

static void thread_pool_thread(int threadIndex) {
	char *prefix;
	asprintf(&prefix, "Thread %d:", threadIndex);

	printf("%s started\n", prefix);

	while (1) {
		sleep(1);
	}
}

static void start_thread_pool() {
	for (int i = 0; i < POOL_COUNT; i++) {
		pthread_t thread_id;
		// look at this shit, an abomination of a function ptr
		if (pthread_create(&thread_id, NULL, (void *(*)(void *)) & thread_pool_thread, (void *)i) != 0) {
			perror("pthread_create");
			exit(1);
		}
	}
}

static void new_client(int fd, struct sockaddr addr, socklen_t addr_len) {
	// Since we want to serve files...
	// http/1.0, dont want multiple http reqeuests per tcp request.

	/*

		accept thread
		client handler thread (reads the request, interprets it, fetches the file and waits for it to be avaliable and
	   passes it back to client, with epoll)

		we must read the request from fd, might be short or long.
		then we interpret it,
		*/

	close(fd);
}

// char res[] = "HTTP/1.0 200 OK\nContent-Type: text/html\n\nHALLO!!";
//
// static void handle_client(int fd, struct sockaddr addr, socklen_t addr_len) {
// 	printf("CLIENT!!\n");
//
// 	char data[4096] = {};
// 	if (read(fd, data, sizeof(data)) == -1) {
// 		handle_error("read");
// 	}
//
// 	printf("got: %.*sEND\n", 4096, data);
// 	if (write(fd, res, sizeof(res) - 1) == -1) {
// 		handle_error("write");
// 	}
//
// 	// poll( (struct pollfd[]) {
// 	// 	(struct pollfd) {
// 	// 		.fd = fd,
// 	// 		.events = 0,
// 	// 		.revents = 0,
// 	// 	}
// 	// }, 1, 60000);
//
// 	if (close(fd) == -1) {
// 		handle_error("close (handle_client)");
// 	}
// }

volatile sig_atomic_t running = 1;
static void handle_sigint(int signum) { running = 0; }

int main(int argc, char **argv) {
	assert(argc > 0);
	if (argc != 2) {
		printf("Usage: %s <path>\n"
			   "where <path> is the dir to serve",
			   argv[0]);
		return 0;
	}

	char *path = argv[1];

	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1) {
		perror("socket");
		return 1;
	}

	int reuseaddropt = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddropt, sizeof(reuseaddropt)) == -1) {
		perror("setsockopt SO_REUSEADDR");
		return 1;
	}

	struct sockaddr_in server_addr = {
		.sin_family = AF_INET,

		// htons converts host byte order (little-endian)
		// to network byte order short (big-endian)
		.sin_port = htons(PORT),

		// htonl converts host byte order (little-endian)
		// to network byte order long
		.sin_addr = {.s_addr = htonl(INADDR_ANY)},
	};

	if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
		perror("bind");
		return 1;
	}

	if (listen(server_fd, LISTEN_BACKLOG) == -1) {
		perror("listen");
		return 1;
	}

	start_thread_pool();

	struct sigaction sa = {.sa_handler = &handle_sigint};
	sigaction(SIGINT, &sa, NULL);
	printf("Listening on port %i (CTRL+C to exit)\n", PORT);

	int client_fd;
	struct sockaddr client_addr;
	socklen_t client_addr_len = sizeof(client_addr);
	while (running) {
		client_fd = accept(server_fd, &client_addr, &client_addr_len);
		if (client_fd == -1) {
			if (errno == EINTR && running == 0) {
				break;
			}
			fprintf(stderr, "accept: %s, continuing\n", strerror(errno));
			continue;
		}

		new_client(client_fd, client_addr, client_addr_len);
	}

	printf(" byeeeee!!\n");
	return 0;
}
