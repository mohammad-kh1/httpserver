#ifndef HTTP_HANDLER_H
#define HTTP_HANDLER_H

#include "http_utils.h" // For http_header struct

// --- Configuration Constants ---
#define BUFFER_SIZE 4096
#define WEB_ROOT "./webroot"

// --- Function Declarations ---

/**
 * @brief Thread entry point. Implements the persistent connection loop.
 */
void *client_handler(void *socket_desc);

/**
 * @brief Handles reading a single request, extracting the URL path, parsing headers, and responding.
 * @return 1 if the connection should be kept open (keep-alive), 0 otherwise.
 */
int process_single_request(int client_sock);

/**
 * @brief Sends an HTTP error response (e.g., 404 Not Found).
 */
void send_error_response(int client_sock, int status_code, const char *status_text, const char *connection_header);

/**
 * @brief Sends a generic 200 OK response, optionally echoing a body.
 */
void send_generic_response(int client_sock, const char* body, const char *connection_header);

/**
 * @brief Attempts to find and send a file located in the WEB_ROOT directory.
 */
void send_file_response(int client_sock, const char* path, const http_header headers[], int num_headers, const char *connection_header);


#endif // HTTP_HANDLER_H
