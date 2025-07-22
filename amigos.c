/**
 * amigos.c
 * A micro Gopher server. Super tiny, ultra portable, single file, standalone,
 * Gopher server written in ANSI C.
 *
 * @author Nathan Campos <nathan@innoveworkshop.com>
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <dirent.h>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>


/* Some common definitions and defaults. */
#define LISTEN_AF        AF_INET
#define LISTEN_ADDR      "0.0.0.0"
#define LISTEN_PORT      70
#define MAX_CONNECTIONS  10
#define RECV_TIMEOUT     3

#define DEFAULT_HOSTNAME "localhost"
#define DEFAULT_PORT     LISTEN_PORT

#define LISTEN_BACKLOG   5
#define INVALID_TYPE     '\0'
#define INVALID_HOST     "null.host"
#define INVALID_PORT     0

#ifdef _WIN32
	#define PATH_SEPARATOR '\\'
#else
	#define PATH_SEPARATOR '/'
#endif /* _WIN32 */

/* Include configuration. */
#include "config.h"

/* Socket abstractions. */
typedef int sockfd_t;
#define SOCKERR -1
#ifndef INET6_ADDRSTRLEN
	#define INET6_ADDRSTRLEN 46
#endif /* !INET6_ADDRSTRLEN */

/**
 * Abstraction of a Gopher item in a listing.
 */
typedef struct gopher_item {
	char type;
	char _pad;
	uint16_t port;
	char *name;
	char *selector;
	char *hostname;
} gopher_item_t;

/**
 * Status flags used by the client_conn_t structure.
 */
enum client_conn_status {
	CONN_FINISHED = 0x01,
	CONN_INUSE    = 0x02
};

/**
 * Client connection thread object.
 */
typedef struct client_conn {
	uint8_t status;
	sockfd_t sockfd;
	pthread_t thread;
	char *selector;
} client_conn_t;


/* Constants for quick validation. */
static char *invalid_host_c;

/* Global state variables. */
static int running;
static char *docroot;
static sockfd_t server_socket;
static client_conn_t connections[MAX_CONNECTIONS];


/* Gopher item operations. */
gopher_item_t* gopher_item_new(void);
void gopher_item_free(gopher_item_t *item);
gopher_item_t* gopher_item_parse(const char *line);
void gopher_item_print(gopher_item_t *item);

/* Server operations. */
sockfd_t server_start(int af, const char *addr, uint16_t port);
void server_loop(int af, sockfd_t sockfd);
void server_stop(void);
void* server_process_request(void *data);
const char* inet_addr_str(int af, void *addr, char *buf);

/* Client operations. */
int client_send_file(const client_conn_t *conn, const char *path);
int client_send_dir(const client_conn_t *conn, const char *path, int header);
int client_send_gophermap(const client_conn_t *conn, const char *path);
int client_send_item(const client_conn_t *conn, const gopher_item_t *item);
int client_send_item_simple(const client_conn_t *conn, char type,
							const char *msg);
int client_send_info(const client_conn_t *conn, const char *msg);
int client_send_error(const client_conn_t *conn, const char *msg);

/* File system utilities. */
int file_exists(const char *fname);
int dir_exists(const char *path);
size_t path_concat(char **buf, ...);
int path_sanitize(char *path);

/* Misc. */
void const_init(void);
void const_free(void);


/**
 * Handles a process signal.
 *
 * @param signum Signal number that was triggered.
 */
void signal_handler(int signum) {
	if (signum == SIGINT)
		server_stop();
}

/**
 * Application's main entry point.
 *
 * @param argc Number of command-line arguments passed.
 * @param argv Command-line arguments.
 *
 * @return Exit code.
 */
int main(int argc, char **argv) {
	int retval;
	int i;

	/* Check if we have a document root folder. */
	if (argc < 2) {
		printf("usage: %s docroot\n", argv[0]);
		return 1;
	}
	docroot = argv[1];

	/* Check if document root folder actually exists. */
	if (!dir_exists(docroot)) {
		printf("ERROR: Document root path '%s' doesn't exist.\n", docroot);
		return 1;
	}

	/* Register signal handler. */
	signal(SIGINT, signal_handler);

	/* Initialize constants and state variables. */
	const_init();
	retval = 0;
	running = 0;
	for (i = 0; i < MAX_CONNECTIONS; i++) {
		connections[i].sockfd = SOCKERR;
		connections[i].status = 0;
		connections[i].thread = NULL;
		connections[i].selector = NULL;
	}

	/* Start server. */
	server_socket = server_start(LISTEN_AF, LISTEN_ADDR, LISTEN_PORT);
	if (server_socket == SOCKERR) {
		retval = 1;
		goto finish;
	}

	/* Run server listen loop. */
	server_loop(LISTEN_AF, server_socket);

finish:
	/* Free resources and exit. */
	if (running)
		server_stop();
	const_free();
	return retval;
}

/**
 * =============================================================================
 * === Server ==================================================================
 * =============================================================================
 */

/**
 * Starts up the server.
 *
 * @param af   Address family.
 * @param addr IP address to bind ourselves to.
 * @param port Port to bind ourselves to.
 *
 * @return Socket file descriptor or SOCKERR if an error occurred.
 */
sockfd_t server_start(int af, const char *addr, uint16_t port) {
	struct sockaddr_storage sa;
	sockfd_t sockfd;
	socklen_t addrlen;
	int flag;
	struct timeval tv;

	/* Zero out address structure and cache its size. */
	memset(&sa, '\0', sizeof(sa));
	addrlen = af == AF_INET ? sizeof(struct sockaddr_in) :
		sizeof(struct sockaddr_in6);

	/* Ensure that we don't have a server already running. */
	if (running != 0) {
		printf("ERROR: Tried to start a server while already running.\n");
		return SOCKERR;
	}

	/* Get a socket file descriptor. */
	sockfd = socket(af == AF_INET ? PF_INET : PF_INET6, SOCK_STREAM, 0);
	if (sockfd == SOCKERR) {
		perror("ERROR: Failed to get a socket file descriptor");
		return SOCKERR;
	}

	/* Populate socket address information. */
	if (af == AF_INET) {
		struct sockaddr_in *inaddr = (struct sockaddr_in*)&sa;
		inaddr->sin_family = af;
		inaddr->sin_port = htons(port);
		inaddr->sin_addr.s_addr = inet_addr(addr);
	} else {
		struct sockaddr_in6 *in6addr = (struct sockaddr_in6*)&sa;
		in6addr->sin6_family = af;
		in6addr->sin6_port = htons(port);
		printf("ERROR: IPv6 not yet implemented.\n");
		return SOCKERR;
	}

	/* Ensure we don't have to worry about address already in use errors. */
	flag = 1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &flag,
			sizeof(flag)) == SOCKERR) {
		perror("ERROR: Failed to set socket address reuse");
		close(sockfd);
		return SOCKERR;
	}

	/* Set a receive timeout so that we don't block indefinitely. */
	tv.tv_sec = RECV_TIMEOUT;
	if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv,
			sizeof(tv)) == SOCKERR) {
		perror("ERROR: Failed to set socket receive timeout");
		close(sockfd);
		return SOCKERR;
	}

	/* Bind address to socket. */
	if (bind(sockfd, (struct sockaddr*)&sa, addrlen) == SOCKERR) {
		perror("ERROR: Failed binding to socket");
		close(sockfd);
		return SOCKERR;
	}

	/* Start listening on our desired socket. */
	if (listen(sockfd, LISTEN_BACKLOG) == SOCKERR) {
		perror("ERROR: Failed to listen on socket");
		close(sockfd);
		return SOCKERR;
	}

	printf("Server running on %s:%u\n", addr, port);
	running = 1;
	return sockfd;
}

/**
 * Stops the server immediately.
 */
void server_stop(void) {
	int i;

	/* Stop the server. */
	printf("Stopping the server...\n");
	running = 0;
	if ((server_socket != SOCKERR) && (close(server_socket) == SOCKERR))
		perror("ERROR: Failed to close server socket");
	server_socket = SOCKERR;

	/* Close all client connections. */
	for (i = 0; i < MAX_CONNECTIONS; i++) {
		if ((connections[i].status & CONN_INUSE) == 0)
			continue;

		connections[i].status = 0;
		if (connections[i].sockfd != SOCKERR)
			close(connections[i].sockfd);
		connections[i].sockfd = SOCKERR;
		if (connections[i].thread != NULL)
			pthread_join(connections[i].thread, NULL);
		connections[i].thread = NULL;
	}
}

/**
 * Server listening loop.
 *
 * @param af     Socket address family.
 * @param sockfd Socket in which the server is listening on.
 */
void server_loop(int af, sockfd_t sockfd) {
	while (running) {
		int i;

		/* Clean up finished requests. */
		for (i = 0; i < MAX_CONNECTIONS; i++) {
			if (connections[i].status & CONN_FINISHED) {
				if (connections[i].thread != NULL)
					pthread_join(connections[i].thread, NULL);
				connections[i].thread = NULL;
				connections[i].selector = NULL;
				connections[i].status = 0;
			}
		}

		/* Check if we can accept new connections at the moment. */
		for (i = 0; i < MAX_CONNECTIONS; i++) {
			client_conn_t *conn;
			struct sockaddr_storage csa;
			socklen_t socklen;
			char addrstr[INET6_ADDRSTRLEN];

			/* Ignore slots currently in use. */
			conn = &connections[i];
			if (conn->status & CONN_INUSE)
				continue;

			/* Accept the client connection. */
			socklen = sizeof(csa);
			conn->sockfd = accept(sockfd, (struct sockaddr*)&csa, &socklen);
			if (conn->sockfd == SOCKERR) {
				if (running)
					perror("ERROR: Failed to accept connection");
				conn->status = 0;
				break;
			}
			conn->status = CONN_INUSE;

			/* Get client address string and announce connection. */
			if (inet_addr_str(af, &csa, addrstr) == NULL) {
				perror("ERROR: Failed to get client address string");
			} else {
				printf("Client connected from %s\n", addrstr);
			}

			/* Process the client's request. */
			if (pthread_create(&conn->thread, NULL, server_process_request,
					conn)) {
				printf("ERROR: Failed to create request processing thread\n");
				close(conn->sockfd);
				conn->status = 0;
				break;
			}

			break;
		}
	}
}

/**
 * Processes a client connection.
 *
 * @warning This function is meant to be called from a thread creation routine.
 *
 * @param data Pointer to a client_conn_t structure.
 */
void* server_process_request(void *data) {
	client_conn_t *conn;
	char selector[256];
	char *fpath;
	ssize_t len;
	int i;

	/* Initialize values. */
	conn = (client_conn_t*)data;
	conn->selector = selector;
	fpath = NULL;

	/* Read the selector from client's request. */
	if ((len = recv(conn->sockfd, selector, 255, 0)) < 0) {
		if (running)
			perror("ERROR: Failed to receive selector");
		goto close_conn;
	}
	selector[len] = '\0';

	/* Ensure the request wasn't too long. */
	if (len >= 255) {
		printf("ERROR: Selector unusually long, closing connection.\n");
		client_send_error(conn, "Selector string longer than 255 characters");
		goto close_conn;
	}

	/* Terminate selector string before CRLF. */
	for (i = 0; i < len; i++) {
		if ((selector[i] == '\t') || (selector[i] == '\r') ||
				(selector[i] == '\n')) {
			selector[i] = '\0';
			break;
		}
	}

	/* Sanitize selector before using it. */
	path_sanitize(selector);
	printf("Client requested selector '%s'\n", selector);

	/* Build local file request path from selector. */
	if (*selector == '\0') {
		fpath = strdup(docroot);
	} else if (path_concat(&fpath, docroot, selector, NULL) == 0) {
		printf("ERROR: Failed to build request path for selector %s\n",
			selector);
		goto close_conn;
	}

	/* Reply to client. */
	if (dir_exists(fpath)) {
		/* Selector matches a directory. */
		char *mapfile;

		/* Check if there's a gophermap file in the directory. */
		if (!path_concat(&mapfile, fpath, "gophermap", NULL))
			goto close_conn;
		if (file_exists(mapfile)) {
			/* Send gophermap. */
			if (!client_send_gophermap(conn, mapfile))
				goto close_conn;
		} else {
			/* List the contents of the directory. */
			if (!client_send_dir(conn, fpath, 1))
				goto close_conn;
		}

		send(conn->sockfd, ".", 1, 0);
	} else if (file_exists(fpath)) {
		/* Selector matches a file. */
		if (!client_send_file(conn, fpath))
			goto close_conn;
	} else {
		/* Looks like the client requested a path that doesn't exist. */
		if (!client_send_error(conn, "Selector not found."))
			goto close_conn;
		send(conn->sockfd, ".", 1, 0);
	}

close_conn:
	/* Free up allocated resources. */
	if (fpath != NULL)
		free(fpath);
	fpath = NULL;

	/* Close the client connection and signal that we are finished here. */
	if (conn->sockfd != SOCKERR)
		close(conn->sockfd);
	conn->sockfd = SOCKERR;
	conn->status |= CONN_FINISHED;

	pthread_exit(NULL);
	return NULL;
}

/**
 * Gets a string representation of a network address structure.
 *
 * @param af   Socket address family.
 * @param addr Network address structure.
 * @param buf  Destination string, pre-allocated to hold an IPv6 address.
 *
 * @return Pointer to the destination string or NULL if an error occurred.
 */
const char* inet_addr_str(int af, void *addr, char *buf) {
	if (af == AF_INET) {
		return inet_ntop(af, &((struct sockaddr_in*)addr)->sin_addr, buf,
			INET6_ADDRSTRLEN);
	} else {
		return inet_ntop(af, &((struct sockaddr_in6*)addr)->sin6_addr, buf,
			INET6_ADDRSTRLEN);
	}
}

/**
 * =============================================================================
 * === Client Replies ==========================================================
 * =============================================================================
 */

/**
 * Replies to the client with the contents of a file.
 *
 * @param conn Client connection object.
 * @param path Path to the file to send to the client.
 *
 * @return TRUE if the operation was successful, FALSE otherwise.
 */
int client_send_file(const client_conn_t *conn, const char *path) {
	FILE *fh;
	size_t flen;
	uint8_t buf[256];
	int ret;

	/* Open file for reading. */
	ret = 1;
	fh = fopen(path, "rb");
	if (fh == NULL) {
		printf("ERROR: Failed to open file %s for request selector '%s'\n",
			path, conn->selector);
		return 0;
	}

	/* Pipe file contents straight to socket. */
	while ((flen = fread(buf, sizeof(char), 256, fh)) > 0) {
		if (send(conn->sockfd, buf, flen, 0) < 0) {
			perror("ERROR: Failed to pipe contents of file to socket");
			ret = 0;
			break;
		}
	}

	/* Close file handle. */
	fclose(fh);
	return ret;
}

/**
 * Replies to the client with a directory listing.
 *
 * @param conn   Client connection object.
 * @param path   Path to the directory to list to the client.
 * @param header Print out a small header giving context to the listing?
 *
 * @return TRUE if the operation was successful, FALSE otherwise.
 */
int client_send_dir(const client_conn_t *conn, const char *path, int header) {
	gopher_item_t *item;
	struct dirent *dirent;
	char name[71];
	DIR *dh;
	int ret;

	/* Open directory. */
	ret = 1;
	dh = opendir(path);
	if (dh == NULL) {
		perror("ERROR: Failed to open directory for listing");
		return 0;
	}

	/* Print out a header. */
	if (header) {
		char msg[256];
		snprintf(msg, 256, "[%s]:", conn->selector);
		ret = client_send_info(conn, msg);
		ret = client_send_info(conn, "");
	}

	/* Set common Gopher item parameters. */
	item = gopher_item_new();
	item->hostname = strdup(DEFAULT_HOSTNAME);
	item->port = DEFAULT_PORT;

	/* Read directory contents. */
	while ((dirent = readdir(dh)) != NULL) {
		/* Skip hidden and special files. */
		if (*dirent->d_name == '.')
			continue;

		/* Skip gophermap files. */
		if (*dirent->d_name == 'g') {
			if (strcmp(dirent->d_name, "gophermap") == 0)
				continue;
		}

		/* Build up Gopher item entry. */
		item->type = dirent->d_type == DT_DIR ? '1' : '0';
		snprintf(name, 71, "%s%c", dirent->d_name,
			(dirent->d_type == DT_DIR ? '/' : ' '));
		item->name = name;
		item->selector = dirent->d_name;

		/* Send the item to the client. */
		if (!client_send_item(conn, item))
			ret = 0;

		/* Free used resources. */
		item->name = NULL;
		item->selector = NULL;
	}

	/* Free up resources. */
	closedir(dh);
	item->name = NULL;
	item->selector = NULL;
	gopher_item_free(item);
	item = NULL;

	return ret;
}

/**
 * Replies to the client with a gophermap.
 *
 * @param conn Client connection object.
 * @param path Path to the gophermap file.
 *
 * @return TRUE if the operation was successful, FALSE otherwise.
 */
int client_send_gophermap(const client_conn_t *conn, const char *path) {
	FILE *fh;
	char buf[256];
	unsigned int linenum;
	int ret;

	/* Open file for reading. */
	ret = 1;
	fh = fopen(path, "r");
	if (fh == NULL) {
		printf("ERROR: Failed to open gophermap for request selector '%s'\n",
			conn->selector);
		return 0;
	}

	/* Read contents line by line. */
	linenum = 0;
	while (fgets(buf, 256, fh) != NULL) {
		gopher_item_t *item;
		char *tmp;
		int tabs;

		/* Strip newline and count tabs. */
		linenum++;
		tmp = buf;
		tabs = 0;
		while ((*tmp != '\r') && (*tmp != '\n') && (*tmp != '\0')) {
			if (*tmp == '\t')
				tabs++;
			tmp++;
		}
		*tmp = '\0';

		/* No tabs means it's an info line, or maybe a special directive. */
		if (tabs == 0) {
			/* Check if it may be a special directive. */
			if (strcmp(buf, ".") == 0) {
				/* Halt file processing. */
				break;
			} else if (strcmp(buf, "*") == 0) {
				/* Render a directory listing. */
				char *dir = strdup(path);
				dir[strlen(path) - 10] = '\0';
				ret = client_send_dir(conn, dir, 0);
				free(dir);
				dir = NULL;
				continue;
			}

			/* Just a regular info line. */
			client_send_info(conn, buf);
			continue;
		}

		/* Parse item line. */
		item = gopher_item_parse(buf);
		if (item == NULL) {
			printf("ERROR: Failed to parse line %u of %s\n", linenum, path);
			client_send_error(conn, "Failed to parse this line of gophermap");
			ret = 0;
			continue;
		}

		/* Send item to client. */
		if (!client_send_item(conn, item))
			ret = 0;
		gopher_item_free(item);
		item = NULL;
	}

	/* Close file handle. */
	fclose(fh);
	return ret;
}

/**
 * Sends an entry item to the client.
 *
 * @param conn Client connection object.
 * @param item Gopher item object to be sent.
 *
 * @return TRUE if the operation was successful, FALSE otherwise.
 */
int client_send_item(const client_conn_t *conn, const gopher_item_t *item) {
	char buf[256];
	size_t len;
	char *selector;

	/* Build up the selector string. */
	selector = NULL;
	if ((*conn->selector != '\0') && (item->selector != NULL) &&
			(item->selector[0] != '/')) {
		if (path_concat(&selector, conn->selector, item->selector, NULL) == 0) {
			printf("ERROR: Failed to concatenate base selector '%s' with "
				"relative selector '%s'\n", conn->selector, item->selector);
			return 0;
		}
	}

	/* Create entry line string for sending to client. */
	len = snprintf(buf, 256, "%c%s\t%s\t%s\t%u\r\n", item->type,
		item->name == NULL ? "" : item->name,
		selector == NULL ? item->selector ? item->selector : "" : selector,
		item->hostname == NULL ? DEFAULT_HOSTNAME : item->hostname,
		item->port);

	/* Free up used selector string. */
	if (selector != NULL)
		free(selector);
	selector = NULL;

	/* Check if the string buffer was big enough. */
	if (len >= 256) {
		printf("ERROR: Entry line too long (>%lu chars) for item '%s'.\n",
			sizeof(buf), item->name);
		return 0;
	}

	/* Send out the entry line. */
	if (send(conn->sockfd, buf, len, 0) < 0) {
		perror("ERROR: Failed to send entry item line");
		return 0;
	}

	return 1;
}

/**
 * Sends a simple message item (no hostname or port) to the client.
 *
 * @param conn Client connection object.
 * @param type Type of item.
 * @param msg  Message to be sent to client.
 *
 * @return TRUE if the operation was successful, FALSE otherwise.
 */
int client_send_item_simple(const client_conn_t *conn, char type,
							const char *msg) {
	int ret;

	/* Populate item object. */
	gopher_item_t *item = gopher_item_new();
	item->type = type;
	item->name = (char*)msg;
	item->hostname = invalid_host_c;
	item->port = INVALID_PORT;

	/* Send item to client. */
	ret = client_send_item(conn, item);

	/* Free up resources. */
	item->name = NULL;
	gopher_item_free(item);

	return ret;
}

/**
 * Sends an info message item to the client.
 *
 * @param conn Client connection object.
 * @param msg  Message to be sent to client.
 *
 * @return TRUE if the operation was successful, FALSE otherwise.
 */
int client_send_info(const client_conn_t *conn, const char *msg) {
	return client_send_item_simple(conn, 'i', msg);
}

/**
 * Sends an error message item to the client.
 *
 * @param conn Client connection object.
 * @param msg  Message to be sent to client.
 *
 * @return TRUE if the operation was successful, FALSE otherwise.
 */
int client_send_error(const client_conn_t *conn, const char *msg) {
	return client_send_item_simple(conn, '3', msg);
}

/**
 * =============================================================================
 * === Gopher Item Abstractions ================================================
 * =============================================================================
 */

/**
 * Allocates a brand new Gopher item object.
 *
 * @warning This function allocates memory that must be free'd using a special
 *          function.
 *
 * @return Allocated item object or NULL if an error occurred.
 *
 * @see gopher_item_free
 */
gopher_item_t* gopher_item_new(void) {
	gopher_item_t *item;

	/* Try to allocate our item object. */
	item = (gopher_item_t*)malloc(sizeof(gopher_item_t));
	if (item == NULL)
		return NULL;

	/* Populate it with sane defaults. */
	item->type = INVALID_TYPE;
	item->_pad = INVALID_TYPE;
	item->name = NULL;
	item->selector = NULL;
	item->hostname = invalid_host_c;
	item->port = INVALID_PORT;

	return item;
}

/**
 * Frees up an Gopher item that was dynamically allocated.
 *
 * @see gopher_item_new
 */
void gopher_item_free(gopher_item_t *item) {
	item->type = INVALID_TYPE;
	if (item->name != NULL)
		free(item->name);
	if (item->selector != NULL)
		free(item->selector);
	if ((item->hostname != invalid_host_c) && (item->hostname != NULL))
		free(item->hostname);
	free(item);
}

/**
 * Parses a Gopher item object from a gophermap line.
 *
 * @warning This function allocates memory that must be free'd using a special
 *          function.
 *
 * @return Allocated item object or NULL if an error occurred.
 *
 * @see gopher_item_free
 */
gopher_item_t* gopher_item_parse(const char *line) {
	gopher_item_t *item;
	const char *tmp;
	char *cbuf;
	char buf[256];

	/* Try to allocate brand new item object. */
	item = gopher_item_new();
	if (item == NULL)
		return NULL;

	/* Populate it with some defaults. */
	item->hostname = strdup(DEFAULT_HOSTNAME);
	item->port = DEFAULT_PORT;

	/* Get item type. */
	tmp = line;
	item->type = *tmp++;

	/* Get item name. */
	cbuf = buf;
	while (*tmp != '\t') {
		*cbuf = *tmp++;
		cbuf++;
	}
	*cbuf = '\0';
	item->name = strdup(buf);
	tmp++;

	/* Get item selector. */
	cbuf = buf;
	while ((*tmp != '\t') && (*tmp != '\0')) {
		*cbuf = *tmp++;
		cbuf++;
	}
	*cbuf = '\0';
	item->selector = strdup(buf);
	if (*tmp == '\0')
		return item;
	tmp++;

	/* Get item hostname. */
	cbuf = buf;
	while ((*tmp != '\t') && (*tmp != '\0')) {
		*cbuf = *tmp++;
		cbuf++;
	}
	*cbuf = '\0';
	item->hostname = strdup(buf);
	if (*tmp == '\0')
		return item;
	tmp++;

	/* Get item port. */
	cbuf = buf;
	while (*tmp != '\0') {
		*cbuf = *tmp++;
		cbuf++;
	}
	*cbuf = '\0';
	item->port = (uint16_t)atoi(buf);

	return item;
}

/**
 * Prints out the contents of a Gopher item object for debugging.
 *
 * @param item Gopher item to be inspected.
 */
void gopher_item_print(gopher_item_t *item) {
	printf("Type:     '%c'\nName:     %s\nSelector: %s\nHostname: %s\nPort:    "
		" %u\n", item->type, item->name, item->selector, item->hostname,
		item->port);
}

/**
 * =============================================================================
 * === File System Utilities ===================================================
 * =============================================================================
 */

/**
 * Checks if a file exists.
 *
 * @param  fname File path to be checked.
 *
 * @return TRUE if the file exists.
 */
int file_exists(const char *fname) {
#ifdef _WIN32
	DWORD dwAttrib;
	LPTSTR szPath;

	/* Should we even check? */
	if (fname == NULL)
		return 0;

	/* Convert the file path to UTF-16. */
	szPath = utf16_mbstowcs(fname);
	if (szPath == NULL)
		return 0;

	/* Get file attributes and return. */
	dwAttrib = GetFileAttributes(szPath);
	free(szPath);
	return (dwAttrib != INVALID_FILE_ATTRIBUTES) &&
		   !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY);
#else
	/* Should we even check? */
	if (fname == NULL)
		return 0;

	return access(fname, F_OK) != -1;
#endif /* _WIN32 */
}

/**
 * Checks if a directory exists and ensures it's actually a directory.
 *
 * @param path Path to a directory to be checked.
 *
 * @return TRUE if the path represents an existing directory.
 */
int dir_exists(const char *path) {
#ifdef _WIN32
	DWORD dwAttrib;
	LPTSTR szPath;

	/* Should we even check? */
	if (path == NULL)
		return 0;

	/* Convert the file path to UTF-16. */
	szPath = utf16_mbstowcs(path);
	if (szPath == NULL)
		return 0;

	/* Get file attributes and return. */
	dwAttrib = GetFileAttributes(szPath);
	free(szPath);
	return (dwAttrib != INVALID_FILE_ATTRIBUTES) &&
		   (dwAttrib & FILE_ATTRIBUTE_DIRECTORY);
#else
	struct stat sb;

	/* Should we even check? */
	if (path == NULL)
		return 0;

	/* Ensure that we can stat the path. */
	if (stat(path, &sb) < 0)
		return 0;

	/* Check if it's actually a directory. */
	return S_ISDIR(sb.st_mode);
#endif /* _WIN32 */
}

/**
 * Sanitizes a path and converts path separators if needed.
 *
 * @param path Path to be sanitized.
 *
 * @return TRUE if path was altered.
 */
int path_sanitize(char *path) {
	int ret;
	char *buf;

	ret = 0;
	buf = path;
	while (*buf != '\0') {
		/* Stop bad actors from doing bad things. */
		if ((*buf == '.') && (*(buf + 1) == '.')) {
			*buf = '\0';
			ret = 1;
			break;
		}

		/* Normalize path separators. */
#ifdef _WIN32
		if (*buf == '/') {
			*buf = '\\';
			ret = 1;
		}
#endif /* _WIN32 */

		buf++;
	}

	return ret;
}

/**
 * Concatenates paths together safely.
 *
 * @warning This function allocates memory that must be free'd by you!
 *
 * @param buf Pointer to final path string. (Allocated internally)
 * @param ... Paths to be concatenated. WARNING: A NULL must be placed at the
 *            end to indicate the end of the list.
 *
 * @return Size of the final buffer or 0 if an error occurred.
 */
size_t path_concat(char **buf, ...) {
	va_list ap;
	size_t len;
	const char *path;
	char *cur;

	/* Initialize things and ensure we leave space for the NULL terminator. */
	*buf = NULL;
	len = 1;

	/* Go through the paths. */
	va_start(ap, buf);
	path = va_arg(ap, char *);
	while (path != NULL) {
		size_t plen;

		/* Reallocate the buffer memory and set the cursor for concatenation. */
		plen = len;
		len += strlen(path);
		*buf = (char *)realloc(*buf, (len + 1) * sizeof(char));
		if (*buf == NULL)
			return 0L;
		cur = (*buf) + plen - 1;

		/* Should we bother prepending the path separator? */
		if ((plen > 1) && (*(cur - 1) != PATH_SEPARATOR)) {
			*cur = PATH_SEPARATOR;
			cur++;
			len++;
		}

		/* Concatenate the next path. */
		while (*path != '\0') {
			*cur = *path;

			cur++;
			path++;
		}

		/* Ensure we NULL terminate the string. */
		*cur = '\0';

		/* Fetch the next path. */
		path = va_arg(ap, char *);
	}
	va_end(ap);

	return len;
}

/**
 * =============================================================================
 * === Lookup Constants ========================================================
 * =============================================================================
 */

/**
 * Initializes our constant variables that will be used later for quick checks.
 */
void const_init(void) {
	invalid_host_c = (char*)malloc((strlen(INVALID_HOST) + 1) * sizeof(char));
	strcpy(invalid_host_c, INVALID_HOST);
}

/**
 * Frees up the memory allocated for our constants.
 */
void const_free(void) {
	free(invalid_host_c);
}
