#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>

// --- Configuration Constants ---
#define MAX_CONNECTIONS 10
#define BUFFER_SIZE 2048
#define PORT_DEFAULT 8080


// This is the template for a 200 OK header, which requires the Content-Length to be inserted.
const char *HTTP_HEADER_200 =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Content-Length: %zu\r\n"
    "Connection: close\r\n" // Still closing connection explicitly for now
    "\r\n"; // End of headers

// --- Forward Declarations ---
void handle_client(int client_sock);
char* extract_path(const char* request);

/**
 * @brief Main entry point for the HTTP server.
 *
 * @param argc The number of command-line arguments.
 * @param argv The command-line arguments (expected: <port>).
 * @return int Exit status.
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
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
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

        // 6. Close the client socket (Temporary: We will implement persistence later)
        printf("[Closing connection] Socket FD: %d\n", new_socket);
        close(new_socket);
    }

    // This part is unreachable in the current infinite loop, but good practice
    close(server_fd);
    return EXIT_SUCCESS;
}

/**
 * @brief Handles reading the request and extracting the URL path.
 *
 * @param client_sock The file descriptor for the connected client socket.
 */
void handle_client(int client_sock) {
    char buffer[BUFFER_SIZE] = {0};
    long valread;

    // Read the client's request into the buffer
    valread = read(client_sock, buffer, BUFFER_SIZE - 1); // -1 to ensure space for null terminator

    if (valread > 0) {
        // Null-terminate the buffer just in case
        buffer[valread] = '\0';
        printf("--- Request Received (%ld bytes) ---\n%s\n--------------------------------------\n", valread, buffer);

        // --- STEP 1: Extract URL Path ---
        char* path = extract_path(buffer);

        if (path) {
            printf("[Path Extracted]: %s\n", path);
            free(path); // Free the dynamically allocated path string
        } else {
            fprintf(stderr, "[Error]: Could not extract a valid path from the request.\n");
        }
    } else if (valread == 0) {
        printf("Client disconnected gracefully.\n");
    } else {
        // An error occurred during the read operation
        if (errno == EPIPE) {
             fprintf(stderr, "Error: Broken pipe (client disconnected unexpectedly).\n");
        } else {
            perror("Read error");
        }
    }
}


/**
 * @brief Constructs a temporary HTML body including the requested path and sends the full HTTP response.
 *
 * @param client_sock The file descriptor for the connected client socket.
 * @param body This parameter is ignored in this simple implementation, but kept for future compatibility.
 * @param path The extracted URL path to be included in the response.
 */
 void send_simple_response(int client_sock , const char* body , const char* path){
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
 * The request line is typically "METHOD /path HTTP/1.1".
 *
 * @param request The full HTTP request string.
 * @return A dynamically allocated string containing the path, or NULL on failure.
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
