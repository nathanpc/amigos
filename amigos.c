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
#include <string.h>
#include <signal.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "config.h"

/* Some common definitions. */
#define DEFAULT_PORT 70
#define INVALID_TYPE '\0'
#define INVALID_HOST "null.host"
#define INVALID_PORT 1

/* Socket abstractions. */
typedef int sockfd_t;
#define SOCKERR -1

/* Constants for quick validation. */
static char *invalid_host_c;

/* Global state variables. */
static int running;
static sockfd_t server_socket;

/**
 * Abstraction of a Gopher item in a listing.
 */
typedef struct gopher_item_s {
	char type;
	char _pad;
	uint16_t port;
	char *name;
	char *selector;
	char *hostname;
} gopher_item_t;


/* Gopher item operations. */
gopher_item_t* gopher_item_new(void);
void gopher_item_free(gopher_item_t *item);
void gopher_item_print(gopher_item_t *item);

/* Server operations. */
sockfd_t server_start(int af, const char *addr, uint16_t port);
void server_stop(void);

/* Misc. */
void const_init(void);
void const_free(void);


/**
 * Handles a process signal.
 *
 * @param signum Signal number that was triggered.
 */
void signal_handler(int signum) {
	if (signum == SIGINT) {
		printf("Got SIGINT\n");
		server_stop();
	}
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
	gopher_item_t *item;
	int retval;
	
	/* Register signal handler. */
	signal(SIGINT, signal_handler);
	
	/* Initialize constants and start server. */
	retval = 0;
	const_init();
	server_socket = server_start(AF_INET, "0.0.0.0", 70);
	if (server_socket == SOCKERR) {
		retval = 1;
		goto finish;
	}
	
	/* Example of an unedited item. */
	item = gopher_item_new();
	gopher_item_print(item);
	printf("\n");
	
	/* Example of a fully populated item. */
	item->type = '1';
	item->name = strdup("An example item");
	item->selector = strdup("/amigos");
	item->hostname = strdup("nathancampos.me");
	item->port = DEFAULT_PORT;
	gopher_item_print(item);
	gopher_item_free(item);

finish:	
	/* Free resources and exit. */
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
	
	/* Zero out the address structure and cache its size. */
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
	
	/* Bind address to socket. */
	if (bind(sockfd, (struct sockaddr*)&sa, addrlen) == SOCKERR) {
		perror("ERROR: Failed binding to socket");
		close(sockfd);
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
	
	/* Start listening on our desired socket. */
	if (listen(sockfd, LISTEN_BACKLOG) == SOCKERR) {
		perror("ERROR: Failed to listen on socket");
		close(sockfd);
		return SOCKERR;
	}
	
	/* TODO: Move to its own function. */
	
	/* Run server loop. */
	printf("Server running on %s:%u\n", addr, port);
	running = 1;
	while (running) {
		sockfd_t clientsock;
		socklen_t socklen;
		char selector[255];
		int len;
		
		/* Accept the client connection. */
		socklen = sizeof(sa);
		clientsock = accept(sockfd, (struct sockaddr*)&sa, &socklen);
		if (clientsock == SOCKERR) {
			perror("ERROR: Failed to accept connection");
			continue;
		}
		
		/* Read the selector from client's request. */
		len = read(clientsock, selector, 254);
		selector[len] = '\0';
		
		printf("Got: '%s'\n", selector);
		close(clientsock);
	}
	
	return sockfd;
}

/**
 * Stops the server immediately.
 */
void server_stop(void) {
	running = 0;
	if ((server_socket != server_socket) && (close(server_socket) == SOCKERR))
		perror("ERROR: Failed to close server socket");
	server_socket = SOCKERR;
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
