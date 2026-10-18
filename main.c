#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define __USE_GNU
#include <dirent.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define PORT 8080
#define POOL_COUNT 3
#define LISTEN_BACKLOG 50
#define MAX_REQUEST_HEADER_SIZE 16384

#define HTTP_OK "HTTP/1.0 200\r\n"
#define HTTP_SERVER "server: china\r\n"
#define HTTP_CACHE "cache-control: private, no-store\r\n"

#define HTTP_BAD_REQ "HTTP/1.0 400\r\n\r\nBAD_REQUEST"

typedef struct {
	int fd;
	struct sockaddr addr;
	socklen_t addr_len;
} job_t;

static char *rootPathName;

static void bad_request(job_t *job) {
	static const char res[] = HTTP_BAD_REQ;
	write(job->fd, res, sizeof(res) - 1);
	close(job->fd);
}

static void interpret_request(job_t *job, char *buffer, size_t buffer_len) {
	char *head = buffer;
	size_t remaining = buffer_len;

	// 3 ("GET") + 1 (" ") + 1 (FIRST CHAR OF PATH)
	if (remaining < 5 || strncmp(head, "GET", 3) != 0) {
		return bad_request(job);
	}
	// Skip "GET" and SPACE
	head += 4;
	remaining -= 4;

	char *path_head = head;
	while (remaining > 0 && head[0] != ' ' && head[0] != '\r' && head[0] != '\n') {
		head++;
		remaining--;
	}
	char path[head - path_head] = {};
	strncpy(path, path_head, sizeof(path));

	static const char res_header[] = HTTP_OK HTTP_SERVER HTTP_CACHE "content-type: text/html\r\n\r\n";
	write(job->fd, res_header, sizeof(res_header));

	write(job->fd, path, sizeof(path));
}

static ssize_t read_request(job_t *job, char *buffer, size_t buffer_max) {
	size_t len = 0;
	while (1) {
		if (len > buffer_max) {
			return -1;
		}
		// 2 new lines mean end of header, one new line is \r\n in the spec
		if (len > 4 && strncmp(buffer + len - 4, "\r\n\r\n", 4) == 0) {
			break;
		}
		// TODO: Timeout after not receiving data for some time, so cant be DDOS attacked
		ssize_t result = read(job->fd, buffer + len, buffer_max - len);
		if (result == -1) {
			perror("read");
			return -1;
		} else if (result == 0) {
			break;
		}
		len += result;
	}
	return len;
}

static job_t next_job;
static bool is_next_job_set = false;
static pthread_mutex_t next_job_mux = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;
static pthread_cond_t next_job_cond = PTHREAD_COND_INITIALIZER;

static void thread_entry(int thread_index) {
	char *prefix;
	asprintf(&prefix, "Thread %d:", thread_index);

	printf("%s started\n", prefix);

	while (1) {
		pthread_mutex_lock(&next_job_mux);
		// Wait until there is a job
		while (!is_next_job_set) {
			pthread_cond_wait(&next_job_cond, &next_job_mux);
		}
		job_t job = next_job;
		is_next_job_set = false;
		// For the main thread
		pthread_cond_broadcast(&next_job_cond);
		pthread_mutex_unlock(&next_job_mux);

		char buffer[MAX_REQUEST_HEADER_SIZE];
		ssize_t buffer_len = read_request(&job, buffer, MAX_REQUEST_HEADER_SIZE);
		if (buffer_len != -1) {
			interpret_request(&job, buffer, buffer_len);
		}
		close(job.fd);
	}
}

volatile sig_atomic_t running = 1;
static void handle_sigint(int signum) { running = 0; }

int main(int argc, char **argv) {
	if (argc != 2) {
		printf("Usage: %s <pathname>\n"
			   "where <pathname> is the dir to serve\n",
			   argv[0]);
		return 0;
	}

	rootPathName = argv[1];
	if (opendir(rootPathName) == NULL) {
		perror("opendir");
		return 1;
	}

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

	for (int i = 0; i < POOL_COUNT; i++) {
		pthread_t id;

		// look at this shit, an abomination of a function ptr
		if (pthread_create(&id, NULL, (void *(*)(void *)) & thread_entry, (void *)(intptr_t)i) != 0) {
			perror("pthread_create");
			return 1;
		}
	}

	struct sigaction sa = {.sa_handler = &handle_sigint};
	sigaction(SIGINT, &sa, NULL);
	printf("Listening on port %i (CTRL+C to exit)\n", PORT);

	// LISTEN_BACKLOG already exists as the queue for the main thread
	// , so its one thread with a queue already set up,
	// just instead what we need to is route the request(s) from main thread
	// to one of the pool threads.

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

		pthread_mutex_lock(&next_job_mux);
		// Wait for the current job to be taken by a pool thread
		while (is_next_job_set) {
			pthread_cond_wait(&next_job_cond, &next_job_mux);
		}
		next_job = (job_t){
			.fd = client_fd,
			.addr = client_addr,
			.addr_len = client_addr_len,
		};
		is_next_job_set = true;
		// The first thread that isnt this thread, which is the pool threads, will aquire and unset is_next_job_set,
		// then all the other ones will continue waiting.
		pthread_cond_broadcast(&next_job_cond);
		pthread_mutex_unlock(&next_job_mux);
	}

	printf("byeeeee!!\n");
	return 0;
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
