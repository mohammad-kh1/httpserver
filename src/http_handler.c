#include "include/http_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>

// --- Response Templates ---
const char *HTTP_200_HEADER_TEMPLATE =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: %s\r\n"
    "Content-Length: %zu\r\n"
    "Connection: %s\r\n"
    "\r\n";

const char *HTTP_ERROR_HEADER_TEMPLATE =
    "HTTP/1.1 %d %s\r\n"
    "Content-Type: text/html\r\n"
    "Content-Length: %zu\r\n"
    "Connection: %s\r\n"
    "\r\n";

/**
 * @brief Thread entry point. Implements the persistent connection loop.
 */
void *client_handler(void *socket_desc) {
    int client_sock = *(int*)socket_desc;
    free(socket_desc);

    int keep_alive = 1;
    printf("[Thread %lu] Starting request loop for socket %d...\n", (unsigned long)pthread_self(), client_sock);

    while (keep_alive) {
        keep_alive = process_single_request(client_sock);

        if (keep_alive) {
            printf("[Thread %lu] Connection kept alive. Waiting for next request...\n", (unsigned long)pthread_self());
        }
    }

    printf("[Thread %lu terminated] Closing connection: %d\n", (unsigned long)pthread_self(), client_sock);
    close(client_sock);

    return NULL;
}

/**
 * @brief Handles reading a single request, extracting the URL path, parsing headers, and responding.
 */
int process_single_request(int client_sock) {
    char buffer[BUFFER_SIZE] = {0};
    long valread;
    http_header request_headers[MAX_HEADERS];
    int num_headers = 0;
    char *request_body_start = NULL;

    const char *content_length_str = NULL;
    size_t content_length = 0;
    size_t body_already_read = 0;
    char method[16] = {0};

    // Read the client's request
    valread = read(client_sock, buffer, BUFFER_SIZE - 1);

    if (valread <= 0) {
        if (valread == 0) printf("Client disconnected gracefully.\n");
        else perror("Read error");
        return 0; // Terminate persistent connection
    }

    buffer[valread] = '\0';
    printf("--- Request Received by Thread %lu (%ld bytes) ---\n%s\n--------------------------------------\n",
           (unsigned long)pthread_self(), valread, buffer);

    // --- 1. Extract Method and Path ---
    char* path = extract_path(buffer);
    if (!path) {
        fprintf(stderr, "[Error]: Could not extract a valid path. Sending 400 error...\n");
        send_error_response(client_sock, 400, "Bad Request", "close");
        return 0;
    }
    sscanf(buffer, "%15s", method);

    // --- 2. Read and Parse Headers ---
    request_body_start = strstr(buffer, "\r\n\r\n");
    if (request_body_start) {
        *request_body_start = '\0';
        request_body_start += 4;
    }
    num_headers = parse_headers(buffer, request_headers, MAX_HEADERS);

    // --- Determine Connection Status ---
    int keep_alive = 1;
    const char *connection_status = "keep-alive";

    const char *conn_header = get_header_value(request_headers, num_headers, "Connection");
    if (conn_header && strcasecmp(conn_header, "close") == 0) {
        keep_alive = 0;
        connection_status = "close";
    }

    // --- 3. Read Request Body (for POST/PUT) ---
    content_length_str = get_header_value(request_headers, num_headers, "Content-Length");
    if (content_length_str) {
        content_length = (size_t)atol(content_length_str);
    }

    char *body_buffer = NULL;
    int is_post_or_put = (strcmp(method, "POST") == 0 || strcmp(method, "PUT") == 0);

    if (content_length > 0 && request_body_start) {
        body_already_read = valread - (request_body_start - buffer);
        if (body_already_read > content_length) { body_already_read = content_length; }

        if (content_length < BUFFER_SIZE * 2) {
            body_buffer = (char *)malloc(content_length + 1);
            if (body_buffer) {
                memcpy(body_buffer, request_body_start, body_already_read);
                body_buffer[content_length] = '\0';

                size_t total_read = body_already_read;
                while (total_read < content_length) {
                    long current_read = read(client_sock, body_buffer + total_read, content_length - total_read);
                    if (current_read <= 0) break;
                    total_read += current_read;
                }
            }
        } else {
            fprintf(stderr, "[Warning]: Request body too large (%zu bytes). Skipping body read.\n", content_length);
        }
    }

    // --- 4. Response (Router) ---
    if (strcmp(method, "GET") == 0) {
        send_file_response(client_sock, path, request_headers, num_headers, connection_status);
    } else if (strcmp(method, "HEAD") == 0) {
        send_generic_response(client_sock, NULL, connection_status);
    } else if (is_post_or_put) {
        send_generic_response(client_sock, body_buffer, connection_status);
    } else {
        send_error_response(client_sock, 501, "Not Implemented", connection_status);
    }

    free(path);
    if (body_buffer) free(body_buffer);

    return keep_alive;
}


/**
 * @brief Sends an HTTP error response (e.g., 404 Not Found).
 */
void send_error_response(int client_sock, int status_code, const char *status_text, const char *connection_header) {
    char body_buffer[BUFFER_SIZE];
    snprintf(body_buffer, BUFFER_SIZE,
             "<html><head><title>%d %s</title></head><body><h1>Error %d: %s</h1><p>The requested resource could not be found.</p></body></html>",
             status_code, status_text, status_code, status_text);

    size_t body_len = strlen(body_buffer);
    char header_buffer[BUFFER_SIZE];

    size_t header_len = snprintf(header_buffer, BUFFER_SIZE, HTTP_ERROR_HEADER_TEMPLATE,
                                 status_code, status_text, body_len, connection_header);

    write(client_sock, header_buffer, header_len);
    write(client_sock, body_buffer, body_len);

    printf("[Response Sent]: %d %s (Connection: %s)\n", status_code, status_text, connection_header);
}

/**
 * @brief Sends a generic 200 OK response, optionally echoing a body.
 */
void send_generic_response(int client_sock, const char* body, const char *connection_header) {
    const char *final_body = body ? body : "<h1>OK</h1><p>Request processed successfully.</p>";

    size_t body_len = strlen(final_body);
    char header_buffer[BUFFER_SIZE];

    size_t header_len = snprintf(header_buffer, BUFFER_SIZE, HTTP_200_HEADER_TEMPLATE,
                                 "text/html", body_len, connection_header);

    write(client_sock, header_buffer, header_len);
    if (body) {
        write(client_sock, final_body, body_len);
    }

    printf("[Response Complete]: 200 OK Generic (Connection: %s, Content-Length: %zu bytes)\n", connection_header, body_len);
}


/**
 * @brief Attempts to find and send a file located in the WEB_ROOT directory.
 */
void send_file_response(int client_sock, const char* path, const http_header headers[], int num_headers, const char *connection_header) {
    char full_path[BUFFER_SIZE];

    if (strstr(path, "..")) {
        send_error_response(client_sock, 403, "Forbidden", connection_header);
        return;
    }

    const char *final_path = path;
    if (strcmp(path, "/") == 0) {
        final_path = "/index.html";
    }

    snprintf(full_path, BUFFER_SIZE, "%s%s", WEB_ROOT, final_path);

    struct stat file_stat;
    if (stat(full_path, &file_stat) == -1) {
        send_error_response(client_sock, 404, "Not Found", connection_header);
        return;
    }
    if (!S_ISREG(file_stat.st_mode)) {
        send_error_response(client_sock, 403, "Forbidden", connection_header);
        return;
    }

    // --- Read entire file into memory ---
    size_t file_size = file_stat.st_size;
    unsigned char *file_content = (unsigned char *)malloc(file_size);
    if (!file_content) {
        send_error_response(client_sock, 500, "Internal Server Error", connection_header);
        return;
    }

    int file_fd = open(full_path, O_RDONLY);
    if (file_fd == -1 || read(file_fd, file_content, file_size) != (ssize_t)file_size) {
        close(file_fd);
        free(file_content);
        send_error_response(client_sock, 500, "Internal Server Error", connection_header);
        return;
    }
    close(file_fd);

    // --- Check for compression eligibility ---
    const char *mime_type = get_mime_type(final_path);
    const char *accept_encoding = get_header_value(headers, num_headers, "Accept-Encoding");

    unsigned char *output_content = file_content;
    size_t output_size = file_size;
    const char *content_encoding = NULL;

    int is_compressible =
        (strcmp(mime_type, "text/html") == 0 ||
         strcmp(mime_type, "text/css") == 0 ||
         strcmp(mime_type, "application/javascript") == 0);

    if (is_compressible && accept_encoding && strstr(accept_encoding, "gzip")) {
        size_t compressed_len = 0;
        unsigned char *compressed_data = compress_data_gzip(file_content, file_size, &compressed_len);

        if (compressed_data && compressed_len > 0 && compressed_len < file_size) {
            output_content = compressed_data;
            output_size = compressed_len;
            content_encoding = "gzip";
            free(file_content);
        } else if (compressed_data) {
            free(compressed_data);
        }
    }

    // --- Build and send Header ---
    char header_buffer[BUFFER_SIZE * 2];
    size_t header_len;

    if (content_encoding) {
        header_len = snprintf(header_buffer, BUFFER_SIZE * 2,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: %s\r\n"
            "Content-Encoding: %s\r\n"
            "Content-Length: %zu\r\n"
            "Connection: %s\r\n"
            "\r\n",
            mime_type, content_encoding, output_size, connection_header);

    } else {
        header_len = snprintf(header_buffer, BUFFER_SIZE * 2, HTTP_200_HEADER_TEMPLATE,
                              mime_type, output_size, connection_header);
    }

    if (write(client_sock, header_buffer, header_len) != -1 &&
        write(client_sock, output_content, output_size) != -1) {
        printf("[Response Complete]: Sent %zu bytes (%s). Connection: %s.\n",
           output_size, content_encoding ? content_encoding : "uncompressed", connection_header);
    } else {
        perror("Error writing response data");
    }

    free(output_content);
}
