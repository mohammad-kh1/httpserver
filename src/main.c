#include <stdio.h>
#include <stdlib.h>
#include "include/http_server.h"

/**
 * @brief Main entry point for the HTTP server.
 */
int main(int argc, char *argv[]) {
    // Determine the port to use
    int port = PORT_DEFAULT;
    if (argc > 1) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port number. Using default %d.\n", PORT_DEFAULT);
            port = PORT_DEFAULT;
        }
    }

    // Call the server's main loop function, defined in http_server.c
    return run_server(port);
}
