#include "include/http_server.h"
#include "include/http_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>

/**
 * @brief Initializes and runs the HTTP server loop.
 */
int run_server(int port) {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    int opt = 1;

    // 1. Create socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        return EXIT_FAILURE;
    }

    // 2. Attach socket to the defined port
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt failed");
        close(server_fd);
        return EXIT_FAILURE;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    // 3. Bind the socket
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        close(server_fd);
        return EXIT_FAILURE;
    }

    // 4. Start listening
    if (listen(server_fd, MAX_CONNECTIONS) < 0) {
        perror("Listen failed");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("--- Simple HTTP Server (Concurrent) ---\n");
    printf("Listening on port %d. Ready to accept connections...\n", port);

    // Main server loop
    while(1) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Accept failed");
            continue;
        }

        int *new_sock_ptr = (int *)malloc(sizeof(int));
        if (new_sock_ptr == NULL) {
            perror("Memory allocation failed for new socket pointer");
            close(new_socket);
            continue;
        }
        *new_sock_ptr = new_socket;

        pthread_t thread_id;

        // Create a new thread to handle the client request
        if (pthread_create(&thread_id, NULL, client_handler, (void*)new_sock_ptr) < 0) {
            perror("Could not create thread");
            free(new_sock_ptr);
            close(new_socket);
            continue;
        }

        pthread_detach(thread_id);
        printf("\n[Connection accepted] Socket FD: %d. Handed off to thread ID: %lu\n", new_socket, (unsigned long)thread_id);
    }

    // Should not be reached
    close(server_fd);
    return EXIT_SUCCESS;
}
