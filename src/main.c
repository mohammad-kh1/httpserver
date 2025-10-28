#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>

// --- Configuration Constants ---
#define MAX_CONNECTIONS 10
#define BUFFER_SIZE 4096 // Increased buffer size to ensure all headers fit
#define PORT_DEFAULT 8080
#define MAX_HEADERS 32
#define MAX_HEADER_LEN 256

// --- Simple Response Data (for Step 2: Respond with body) ---
const char *HTTP_HEADER_200 =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Content-Length: %zu\r\n"
    "Connection: close\r\n"
    "\r\n";

// --- Data Structures ---
typedef struct {
    char key[MAX_HEADER_LEN];
    char value[MAX_HEADER_LEN];
} http_header;

// --- Forward Declarations ---
void handle_client(int client_sock);
char* extract_path(const char* request);
int parse_headers(char* request_buffer, http_header headers[], int max_headers);
void send_simple_response(int client_sock, const char* body, const char* path);

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
    // FIX: Removed SO_REUSEPORT which can cause compilation errors on some systems.
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

    printf("--- Simple HTTP Server ---\n");
    printf("Listening on port %d. Ready to accept connections...\n", port);

    // Main server loop
    while(1) {
        // 5. Accept an incoming connection
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Accept failed");
            continue; // Go back and wait for the next connection
        }

        printf("\n[Connection accepted] Socket FD: %d\n", new_socket);

        // Process the request
        handle_client(new_socket);

        // 6. Close the client socket (Temporary)
        printf("[Closing connection] Socket FD: %d\n", new_socket);
        close(new_socket);
    }

    // Unreachable
    close(server_fd);
    return EXIT_SUCCESS;
}

/**
 * @brief Handles reading the request, extracting the URL path, parsing headers, and responding.
 *
 * @param client_sock The file descriptor for the connected client socket.
 */
void handle_client(int client_sock) {
    char buffer[BUFFER_SIZE] = {0};
    long valread;
    http_header request_headers[MAX_HEADERS];
    int num_headers = 0;
    char *request_body_start = NULL; // Pointer to the start of the body (if any)

    // Read the client's request into the buffer
    valread = read(client_sock, buffer, BUFFER_SIZE - 1);

    if (valread > 0) {
        buffer[valread] = '\0';
        printf("--- Request Received (%ld bytes) ---\n%s\n--------------------------------------\n", valread, buffer);

        // --- STEP 1: Extract URL Path ---
        char* path = extract_path(buffer);

        // --- STEP 3: Read header ---
        // Find the boundary between headers and body: the first occurrence of \r\n\r\n
        request_body_start = strstr(buffer, "\r\n\r\n");
        if (request_body_start) {
            // Temporarily null-terminate the buffer right before the body starts
            // to isolate the headers for parsing.
            *request_body_start = '\0';
            request_body_start += 4; // Move pointer past \r\n\r\n to the start of the body
        }

        // Parse the headers from the header-only part of the buffer
        num_headers = parse_headers(buffer, request_headers, MAX_HEADERS);

        printf("[Headers Parsed (%d total)]:\n", num_headers);
        for (int i = 0; i < num_headers; i++) {
            printf("  - %s: %s\n", request_headers[i].key, request_headers[i].value);
        }

        // Restore the buffer state if we plan to read the body later (not critical now)
        if (request_body_start) {
             *(request_body_start - 4) = '\r'; // Restore the buffer
        }


        if (path) {
            printf("[Path Extracted]: %s\n", path);
            // --- STEP 2: Respond with body ---
            send_simple_response(client_sock, NULL, path);
            free(path);
        } else {
            fprintf(stderr, "[Error]: Could not extract a valid path. Sending 400 error...\n");
            const char *bad_request = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            write(client_sock, bad_request, strlen(bad_request));
        }
    } else if (valread == 0) {
        printf("Client disconnected gracefully.\n");
    } else {
        if (errno == EPIPE) {
             fprintf(stderr, "Error: Broken pipe (client disconnected unexpectedly).\n");
        } else {
            perror("Read error");
        }
    }
}

/**
 * @brief Parses the HTTP request buffer to extract individual header key-value pairs.
 * NOTE: This function modifies the input buffer using string manipulation.
 *
 * @param request_buffer The buffer containing the request headers (null-terminated before the body).
 * @param headers An array to store the parsed headers.
 * @param max_headers The maximum number of headers to store.
 * @return The number of headers successfully parsed.
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
            // Copy key, trimming leading/trailing spaces would be necessary for full compliance
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
 * @brief Constructs a temporary HTML body including the requested path and sends the full HTTP response.
 */
void send_simple_response(int client_sock, const char* body, const char* path) {
    (void)body; // Silence unused parameter warning for now

    // Dynamically generate the HTML body content to include the path
    char dynamic_body[BUFFER_SIZE];
    // Create an HTML response that echoes the path the user requested
    snprintf(dynamic_body, BUFFER_SIZE,
             "<html><head><title>C Server</title></head><body><h1>Hello from C Server!</h1><p>Path requested: <strong>%s</strong></p></body></html>",
             path);

    size_t body_len = strlen(dynamic_body);

    // Construct the full HTTP header
    char header_buffer[BUFFER_SIZE];
    // Use snprintf to format the header, inserting the correct Content-Length
    size_t header_len = snprintf(header_buffer, BUFFER_SIZE, HTTP_HEADER_200, body_len);

    // Send the header
    if (write(client_sock, header_buffer, header_len) == -1) {
        perror("Error writing response header");
        return;
    }

    // Send the body
    if (write(client_sock, dynamic_body, body_len) == -1) {
        perror("Error writing response body");
        return;
    }

    printf("[Response Sent]: 200 OK (Content-Length: %zu bytes)\n", body_len);
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
