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

/* Some common definitions. */
#define DEFAULT_PORT 70
#define INVALID_TYPE '\0'
#define INVALID_HOST "null.host"
#define INVALID_PORT 1

/* Constants for quick validation. */
static char *invalid_host_c;

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

/* Misc. */
void const_init(void);
void const_free(void);


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
	
	/* Initialize constants. */
	const_init();
	
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
	
	/* Free constants and exit. */
	const_free();
	return 0;
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
