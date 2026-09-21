#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#define __USE_GNU
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 8080
#define LISTEN_BACKLOG 50

static void client_handler_thread() {

}

static void handle_new_client(int fd, struct sockaddr addr, socklen_t addr_len) {
	// Since we want to serve files...
	// http/1.0, dont want multiple http reqeuests per tcp request.

	/*

		accept thread
		client handler thread (reads the request, interprets it, fetches the file and waits for it to be avaliable and passes it back to client, with epoll)

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
static void handle_sigint(int signum) {
	running = 0;
}

int main(int argc, char** argv) {
	assert(argc > 0);
	if (argc != 2) {
		printf(
			"Usage: %s <dir path>\n"
			"where dir path is the dir you want to serve",
			argv[0]);
		exit(0);
	}

	char* path = argv[1];

	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1) {
		perror("socket");
		exit(1);
	}

	int reuseaddropt = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddropt, sizeof(reuseaddropt)) == -1) {
		perror("setsockopt SO_REUSEADDR");
		exit(1);
	}

	struct sockaddr_in server_addr = {
		.sin_family = AF_INET,
		// htons converts host byte order (little-endian)
		// to network byte order short (big-endian)
		.sin_port = htons(8080),
		// htonl converts host byte order (little-endian)
		// to network byte order long
		.sin_addr = {.s_addr = htonl(INADDR_ANY)},
	};

	if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
		perror("bind");
		exit(1);
	}

	if (listen(server_fd, LISTEN_BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}

	signal(SIGINT, &handle_sigint);
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

		// This can only handle 1 client at a time, i can make it 1 thread per
		// client, or a pool. maybe ill do a pool later.
		handle_client(client_fd, client_addr, client_addr_len);
	}

	printf("byeeeee!!\n");
	return 1;
}
