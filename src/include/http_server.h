#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

// --- Configuration Constants ---
#define MAX_CONNECTIONS 10
#define PORT_DEFAULT 8080

// --- Function Declarations ---

/**
 * @brief Initializes and runs the HTTP server loop.
 * @param port The port number to listen on.
 */
int run_server(int port);

#endif // HTTP_SERVER_H
