#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h> // For stat() to get file size/existence
#include <fcntl.h>    // For open()
#include <zlib.h>     // For Gzip compression
#include <strings.h>  // For strcasecmp (case-insensitive string comparison)

// --- Configuration Constants ---
#define MAX_CONNECTIONS 10
#define BUFFER_SIZE 4096
#define PORT_DEFAULT 8080
#define MAX_HEADERS 32
#define MAX_HEADER_LEN 256
#define WEB_ROOT "./webroot" // The directory to serve files from

// --- Response Templates (Step 11: Updated to use %s for dynamic Connection header) ---
const char *HTTP_200_HEADER_TEMPLATE =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: %s\r\n"
    "Content-Length: %zu\r\n"
    "Connection: %s\r\n" // Placeholder for keep-alive or close
    "\r\n";

const char *HTTP_ERROR_HEADER_TEMPLATE =
    "HTTP/1.1 %d %s\r\n"
    "Content-Type: text/html\r\n"
    "Content-Length: %zu\r\n"
    "Connection: %s\r\n" // Placeholder for keep-alive or close
    "\r\n";

// --- Data Structures ---
typedef struct {
    char key[MAX_HEADER_LEN];
    char value[MAX_HEADER_LEN];
} http_header;

// --- Forward Declarations ---
int process_single_request(int client_sock); // New function to handle one request cycle
void *client_handler(void *socket_desc);
char* extract_path(const char* request);
int parse_headers(char* request_buffer, http_header headers[], int max_headers);
const char* get_header_value(const http_header headers[], int num_headers, const char* key);
void send_error_response(int client_sock, int status_code, const char *status_text, const char *connection_header);
void send_file_response(int client_sock, const char* path, const http_header headers[], int num_headers, const char *connection_header);
void send_generic_response(int client_sock, const char* body, const char *connection_header);
const char *get_mime_type(const char *path);
unsigned char* compress_data_gzip(const unsigned char *data, size_t data_len, size_t *compressed_len);


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

    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    int opt = 1;

    // 1. Create socket file descriptor: IPv4 (AF_INET), TCP (SOCK_STREAM)
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // 2. Attach socket to the defined port (SO_REUSEADDR helps with rapid restart)
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    // Configure the server address structure
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Listen on all available interfaces
    address.sin_port = htons(port);       // Convert port number to network byte order

    // 3. Bind the socket to the port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // 4. Start listening for incoming connections
    if (listen(server_fd, MAX_CONNECTIONS) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("--- Simple HTTP Server (Concurrent) ---\n");
    printf("Serving files from: %s\n", WEB_ROOT);
    printf("Listening on port %d. Ready to accept connections...\n", port);

    // Main server loop
    while(1) {
        // 5. Accept an incoming connection
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Accept failed");
            continue;
        }

        // Allocate memory for the socket descriptor to safely pass it to the thread
        int *new_sock_ptr = (int *)malloc(sizeof(int));
        if (new_sock_ptr == NULL) {
            perror("Memory allocation failed for new socket pointer");
            close(new_socket);
            continue;
        }
        *new_sock_ptr = new_socket;

        pthread_t thread_id;

        // 6. Create a new thread to handle the client request concurrently
        if (pthread_create(&thread_id, NULL, client_handler, (void*)new_sock_ptr) < 0) {
            perror("Could not create thread");
            free(new_sock_ptr);
            close(new_socket);
            continue;
        }

        // Detach the thread (allows resources to be reclaimed automatically)
        pthread_detach(thread_id);

        printf("\n[Connection accepted] Socket FD: %d. Handed off to thread ID: %lu\n", new_socket, (unsigned long)thread_id);
    }

    // Unreachable
    close(server_fd);
    return EXIT_SUCCESS;
}

/**
 * @brief Thread entry point. Implements the persistent connection loop.
 */
void *client_handler(void *socket_desc) {
    int client_sock = *(int*)socket_desc;
    free(socket_desc);

    int keep_alive = 1;
    printf("[Thread %lu] Starting request loop for socket %d...\n", (unsigned long)pthread_self(), client_sock);

    // Keep processing requests until a read error/disconnect occurs
    // or the request/response explicitly specifies 'Connection: close'
    while (keep_alive) {
        keep_alive = process_single_request(client_sock);

        if (keep_alive) {
            // Note: In a real server, a timeout should be implemented here.
            printf("[Thread %lu] Connection kept alive. Waiting for next request...\n", (unsigned long)pthread_self());
        }
    }

    printf("[Thread %lu terminated] Closing connection: %d\n", (unsigned long)pthread_self(), client_sock);
    close(client_sock);

    return NULL;
}


/**
 * @brief Handles reading a single request, extracting the URL path, parsing headers, and responding.
 * Returns 1 if the connection should be kept open (keep-alive), 0 otherwise.
 */
int process_single_request(int client_sock) {
    char buffer[BUFFER_SIZE] = {0};
    long valread;
    http_header request_headers[MAX_HEADERS];
    int num_headers = 0;

    // Pointer to the exact position in the buffer where the request body starts (after \r\n\r\n)
    char *request_body_start = NULL;

    // Variables for Step 6: Read request body
    const char *content_length_str = NULL;
    size_t content_length = 0;
    size_t body_already_read = 0;
    char method[16] = {0}; // e.g., "GET", "POST", "PUT"

    // Read the client's request into the buffer
    valread = read(client_sock, buffer, BUFFER_SIZE - 1);

    if (valread <= 0) {
        // Client disconnected or error/timeout
        if (valread == 0) printf("Client disconnected gracefully.\n");
        else perror("Read error");
        return 0; // Terminate persistent connection
    }

    buffer[valread] = '\0';
    printf("--- Request Received by Thread %lu (%ld bytes) ---\n%s\n--------------------------------------\n",
           (unsigned long)pthread_self(), valread, buffer);

    // --- 1. Extract Method and Path (from Request-Line) ---
    char* path = extract_path(buffer);
    if (!path) {
        fprintf(stderr, "[Error]: Could not extract a valid path. Sending 400 error...\n");
        send_error_response(client_sock, 400, "Bad Request", "close"); // Send 'close' on malformed request
        return 0;
    }
    printf("[Path Extracted]: %s\n", path);

    // Extract the method (the first token)
    sscanf(buffer, "%15s", method); // Safe scan up to 15 chars for method

    // --- 2. Read and Parse Headers ---
    request_body_start = strstr(buffer, "\r\n\r\n");
    if (request_body_start) {
        // Temporarily null-terminate the buffer right before the body starts to isolate headers
        *request_body_start = '\0';
        request_body_start += 4; // Move pointer past \r\n\r\n to the start of the body
    }

    num_headers = parse_headers(buffer, request_headers, MAX_HEADERS);

    printf("[Headers Parsed (%d total)]:\n", num_headers);
    for (int i = 0; i < num_headers; i++) {
        printf("  - %s: %s\n", request_headers[i].key, request_headers[i].value);
    }

    // --- Determine Connection Status ---
    int keep_alive = 1; // Default to keep-alive for HTTP/1.1
    const char *connection_status = "keep-alive";

    const char *conn_header = get_header_value(request_headers, num_headers, "Connection");
    if (conn_header && strcasecmp(conn_header, "close") == 0) {
        keep_alive = 0;
        connection_status = "close";
    }

    printf("[Connection Status]: %s\n", connection_status);

    // --- 3. Read Request Body (for POST/PUT) ---
    content_length_str = get_header_value(request_headers, num_headers, "Content-Length");
    if (content_length_str) {
        content_length = (size_t)atol(content_length_str);
    }

    char *body_buffer = NULL; // Initialize body buffer for POST/PUT data
    int is_post_or_put = (strcmp(method, "POST") == 0 || strcmp(method, "PUT") == 0);

    if (content_length > 0 && request_body_start) {
        body_already_read = valread - (request_body_start - buffer);
        if (body_already_read > content_length) { body_already_read = content_length; }

        size_t bytes_remaining = content_length - body_already_read;

        printf("[Body Expected]: %zu bytes (Already read: %zu, Remaining: %zu)\n",
               content_length, body_already_read, bytes_remaining);

        if (content_length < BUFFER_SIZE * 2) { // Arbitrary limit for demo purposes
            body_buffer = (char *)malloc(content_length + 1);
            if (body_buffer) {
                // Copy the part of the body already read from the initial buffer
                memcpy(body_buffer, request_body_start, body_already_read);
                body_buffer[content_length] = '\0';

                // Read the rest of the body, if any
                size_t total_read = body_already_read;
                while (total_read < content_length) {
                    long current_read = read(client_sock, body_buffer + total_read, content_length - total_read);
                    if (current_read <= 0) {
                        perror("Error reading remaining body");
                        break;
                    }
                    total_read += current_read;
                }

                printf("--- Request Body (%zu bytes total) ---\n%s\n--------------------------------------\n",
                       total_read, body_buffer);
            }
        } else {
            // FIX: Pass content_length to match the %zu format specifier
            fprintf(stderr, "[Warning]: Request body too large (%zu bytes). Skipping body read.\n", content_length);
        }

    } else if (is_post_or_put) {
         fprintf(stderr, "[Warning]: POST/PUT received without Content-Length header or body.\n");
    }

    // --- 4. Response (Router) ---
    if (strcmp(method, "GET") == 0) {
        send_file_response(client_sock, path, request_headers, num_headers, connection_status);
    } else if (strcmp(method, "HEAD") == 0) {
        // HEAD requests are just like GET, but without the body
        send_generic_response(client_sock, NULL, connection_status);
    } else if (is_post_or_put) {
        // For POST/PUT, send a generic echo response (Step 6 logic)
        send_generic_response(client_sock, body_buffer, connection_status);
    } else {
        // Catch-all for unsupported methods (DELETE, OPTIONS, etc.)
        send_error_response(client_sock, 501, "Not Implemented", connection_status);
    }


    free(path);
    if (body_buffer) free(body_buffer);

    // Return the determined status for the client_handler loop
    return keep_alive;
}

/**
 * @brief Utility to retrieve a specific header value.
 */
const char* get_header_value(const http_header headers[], int num_headers, const char* key) {
    for (int i = 0; i < num_headers; i++) {
        // Use case-insensitive string comparison (strcasecmp)
        if (strcasecmp(headers[i].key, key) == 0) {
            return headers[i].value;
        }
    }
    return NULL;
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

    // Format the error header with the correct Content-Length and dynamic Connection
    size_t header_len = snprintf(header_buffer, BUFFER_SIZE, HTTP_ERROR_HEADER_TEMPLATE,
                                 status_code, status_text, body_len, connection_header);

    write(client_sock, header_buffer, header_len);
    write(client_sock, body_buffer, body_len);

    printf("[Response Sent by Thread %lu]: %d %s (Connection: %s)\n",
           (unsigned long)pthread_self(), status_code, status_text, connection_header);
}

/**
 * @brief Sends a generic 200 OK response, optionally echoing a body.
 */
void send_generic_response(int client_sock, const char* body, const char *connection_header) {
    // If body is NULL, this is likely a HEAD request
    const char *final_body = body ? body : "<h1>OK</h1><p>Request processed successfully.</p>";

    size_t body_len = strlen(final_body);
    char header_buffer[BUFFER_SIZE];

    // Format the 200 header with text/html, Content-Length, and dynamic Connection
    size_t header_len = snprintf(header_buffer, BUFFER_SIZE, HTTP_200_HEADER_TEMPLATE,
                                 "text/html", body_len, connection_header);

    write(client_sock, header_buffer, header_len);
    // Only write the body if it's not a HEAD request (or if the body is explicitly set)
    if (body) {
        write(client_sock, final_body, body_len);
    }

    printf("[Response Sent by Thread %lu]: 200 OK Generic (Connection: %s, Content-Length: %zu bytes)\n",
           (unsigned long)pthread_self(), connection_header, body_len);
}


/**
 * @brief Attempts to find and send a file located in the WEB_ROOT directory.
 * Includes support for Gzip compression and dynamic connection header.
 */
void send_file_response(int client_sock, const char* path, const http_header headers[], int num_headers, const char *connection_header) {
    char full_path[BUFFER_SIZE];

    // Prevent directory traversal (basic check: look for ".." in the path)
    if (strstr(path, "..")) {
        send_error_response(client_sock, 403, "Forbidden", connection_header);
        return;
    }

    // Default to index.html if the path is just '/'
    const char *final_path = path;
    if (strcmp(path, "/") == 0) {
        final_path = "/index.html";
        printf("[Mapping Path]: '/' mapped to '%s'\n", final_path);
    }

    // Construct the full filesystem path: WEB_ROOT + final_path
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

    // --- Step 1: Read entire file into memory ---
    size_t file_size = file_stat.st_size;
    unsigned char *file_content = (unsigned char *)malloc(file_size);
    if (!file_content) {
        perror("Memory allocation failed for file content");
        send_error_response(client_sock, 500, "Internal Server Error", connection_header);
        return;
    }

    int file_fd = open(full_path, O_RDONLY);
    if (file_fd == -1) {
        perror("Error opening file");
        free(file_content);
        send_error_response(client_sock, 500, "Internal Server Error", connection_header);
        return;
    }

    // Read the file content
    if (read(file_fd, file_content, file_size) != (ssize_t)file_size) {
        perror("Error reading file content fully");
        close(file_fd);
        free(file_content);
        send_error_response(client_sock, 500, "Internal Server Error", connection_header);
        return;
    }
    close(file_fd);

    // --- Step 2: Check for compression eligibility ---
    const char *mime_type = get_mime_type(final_path);
    const char *accept_encoding = get_header_value(headers, num_headers, "Accept-Encoding");

    unsigned char *output_content = file_content;
    size_t output_size = file_size;
    const char *content_encoding = NULL;

    // Check if the file type is compressible (text-based: HTML, CSS, JS)
    int is_compressible =
        (strcmp(mime_type, "text/html") == 0 ||
         strcmp(mime_type, "text/css") == 0 ||
         strcmp(mime_type, "application/javascript") == 0);

    if (is_compressible && accept_encoding && strstr(accept_encoding, "gzip")) {

        size_t compressed_len = 0;
        unsigned char *compressed_data = compress_data_gzip(file_content, file_size, &compressed_len);

        // Only use compressed data if compression was successful AND provided a benefit
        if (compressed_data && compressed_len > 0 && compressed_len < file_size) {
            output_content = compressed_data;
            output_size = compressed_len;
            content_encoding = "gzip";

            printf("[Compression]: File compressed from %zu to %zu bytes (Ratio: %.2f%%)\n",
                   file_size, compressed_len, (double)compressed_len / file_size * 100.0);

            // Free original content memory, as we now use the compressed data
            free(file_content);
        } else {
            // Clean up failed compression attempt memory, continue with original file_content
            if (compressed_data) free(compressed_data);
            printf("[Compression]: Gzip failed or was ineffective. Sending uncompressed.\n");
        }
    }

    // --- Step 3: Build and send Header ---
    char header_buffer[BUFFER_SIZE * 2];
    size_t header_len;

    if (content_encoding) {
        // Dynamically create the header string to include Content-Encoding
        header_len = snprintf(header_buffer, BUFFER_SIZE * 2,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: %s\r\n"
            "Content-Encoding: %s\r\n"
            "Content-Length: %zu\r\n"
            "Connection: %s\r\n"
            "\r\n",
            mime_type, content_encoding, output_size, connection_header);

    } else {
        // Response for Uncompressed Content (Uses the dynamic template)
        header_len = snprintf(header_buffer, BUFFER_SIZE * 2, HTTP_200_HEADER_TEMPLATE,
                              mime_type, output_size, connection_header);
    }

    if (write(client_sock, header_buffer, header_len) == -1) {
        perror("Error writing response header");
        goto cleanup;
    }

    // --- Step 4: Send Content ---
    if (write(client_sock, output_content, output_size) == -1) {
        perror("Error writing file content");
    }

    cleanup:
    // Free the content buffer, whether it was original or compressed
    free(output_content);

    printf("[Response Complete]: Sent %zu bytes (%s). Connection: %s.\n",
           output_size, content_encoding ? content_encoding : "uncompressed", connection_header);
}


/**
 * @brief Simple utility to determine the MIME type based on file extension.
 */
const char *get_mime_type(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";

    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) return "text/html";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".js") == 0) return "application/javascript";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".gif") == 0) return "image/gif";
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".pdf") == 0) return "application/pdf";
    if (strcmp(ext, ".ico") == 0) return "image/x-icon";

    // Default MIME type for unknown extensions
    return "application/octet-stream";
}

/**
 * @brief Parses the HTTP request buffer to extract individual header key-value pairs.
 */
int parse_headers(char* request_buffer, http_header headers[], int max_headers) {
    int count = 0;
    char *line_start = request_buffer;
    char *line_end;

    // Skip the first line (the Request-Line: METHOD /path HTTP/1.1)
    line_end = strstr(line_start, "\r\n");
    if (!line_end) return 0;
    line_start = line_end + 2; // Start parsing from the second line

    // Iterate through lines until the header block ends (empty line or max headers reached)
    while (line_start && count < max_headers) {
        line_end = strstr(line_start, "\r\n");
        if (!line_end || line_end == line_start) break; // End of headers (\r\n\r\n)

        // Null-terminate the current header line
        *line_end = '\0';

        // Find the colon separator
        char *colon = strchr(line_start, ':');
        if (colon) {
            // Extract Key
            size_t key_len = colon - line_start;
            strncpy(headers[count].key, line_start, key_len < MAX_HEADER_LEN - 1 ? key_len : MAX_HEADER_LEN - 1);
            headers[count].key[key_len < MAX_HEADER_LEN - 1 ? key_len : MAX_HEADER_LEN - 1] = '\0';

            // Extract Value
            char *value_start = colon + 1;
            // Skip leading whitespace in the value
            while (*value_start == ' ' || *value_start == '\t') {
                value_start++;
            }

            // Copy value
            size_t val_len = strlen(value_start);
            strncpy(headers[count].value, value_start, val_len < MAX_HEADER_LEN - 1 ? val_len : MAX_HEADER_LEN - 1);
            headers[count].value[val_len < MAX_HEADER_LEN - 1 ? val_len : MAX_HEADER_LEN - 1] = '\0';

            count++;
        }

        line_start = line_end + 2; // Move to the next line
    }

    return count;
}


/**
 * @brief Extracts the request path (e.g., "/index.html") from the HTTP request line.
 */
char* extract_path(const char* request) {
    if (!request || strlen(request) == 0) return NULL;

    // 1. Find the first space, which separates the METHOD (GET, POST, etc.) from the path.
    const char *start = strchr(request, ' ');
    if (!start) return NULL;
    start++; // Move past the first space (now pointing at '/')

    // 2. Find the second space, which separates the path from the HTTP version.
    const char *end = strchr(start, ' ');
    if (!end || end <= start) return NULL;

    // 3. Calculate length, allocate memory, and copy.
    size_t path_len = end - start;
    char *path = (char *)malloc(path_len + 1);
    if (!path) {
        perror("Memory allocation failed in extract_path");
        return NULL;
    }

    // Copy the path substring and null-terminate it
    strncpy(path, start, path_len);
    path[path_len] = '\0';

    return path;
}

/**
 * @brief Uses zlib's compress2 to compress the given data.
 */
unsigned char* compress_data_gzip(const unsigned char *data, size_t data_len, size_t *compressed_len) {
    if (data_len == 0) {
        *compressed_len = 0;
        return NULL;
    }

    // Determine the maximum size for the compressed buffer
    unsigned long bound = compressBound(data_len);

    unsigned char *compressed_data = (unsigned char *)malloc(bound);
    if (!compressed_data) {
        perror("Memory allocation failed for compressed buffer");
        *compressed_len = 0;
        return NULL;
    }

    // Use compress2 for simple, single-shot compression.
    unsigned long dest_len = bound;
    int compress_result = compress2(compressed_data, &dest_len, data, data_len, Z_DEFAULT_COMPRESSION);

    if (compress_result == Z_OK) {
        *compressed_len = (size_t)dest_len;
        return compressed_data;
    } else {
        fprintf(stderr, "Compression failed. Error code: %d\n", compress_result);
        free(compressed_data);
        *compressed_len = 0;
        return NULL;
    }
}
