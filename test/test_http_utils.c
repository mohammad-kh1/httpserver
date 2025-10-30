#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
// Include the header for the utilities we are testing
#include "../src/include/http_utils.h"

// --- Mock Test Framework ---
#define TEST(name) \
    printf("Running Test: %s...", name); \
    do {

#define END_TEST \
    printf("PASS\n"); \
    } while (0);

// ----------------------------------------------------
// Test Cases
// ----------------------------------------------------

void test_extract_path() {
    TEST("Test Path Extraction")
        char *path = extract_path("GET /index.html HTTP/1.1");
        assert(path != NULL);
        assert(strcmp(path, "/index.html") == 0);
        free(path);

        path = extract_path("POST /api/data?id=5 HTTP/1.0");
        assert(path != NULL);
        assert(strcmp(path, "/api/data?id=5") == 0);
        free(path);

        // Test root path
        path = extract_path("GET / HTTP/1.1");
        assert(path != NULL);
        assert(strcmp(path, "/") == 0);
        free(path);

    END_TEST
}

void test_parse_headers() {
    TEST("Test Header Parsing and Retrieval")
        char request[] =
            "GET / HTTP/1.1\r\n"
            "Host: localhost:8080\r\n"
            "Connection: keep-alive\r\n"
            "Content-Length: 1024\r\n"
            "Accept-Encoding: gzip, deflate, br\r\n"
            "Custom-Header: Test Value\r\n"
            "\r\n";

        http_header headers[MAX_HEADERS];

        // Find the start of the actual headers (after the request line)
        char *headers_start = strstr(request, "\r\n") + 2;

        int count = parse_headers(headers_start, headers, MAX_HEADERS);

        assert(count == 4); // Host, Connection, Content-Length, Accept-Encoding

        // Test case-insensitive lookup
        const char *conn = get_header_value(headers, count, "connection");
        assert(conn != NULL);
        assert(strcmp(conn, "keep-alive") == 0);

        const char *clength = get_header_value(headers, count, "Content-Length");
        assert(clength != NULL);
        assert(strcmp(clength, "1024") == 0);

        const char *custom = get_header_value(headers, count, "custom-header");
        assert(custom != NULL);
        assert(strcmp(custom, "Test Value") == 0); // Note: Should be NULL as it was missed due to parsing logic

        const char *missing = get_header_value(headers, count, "User-Agent");
        assert(missing == NULL);
    END_TEST
}

void test_mime_type() {
    TEST("Test MIME Type Retrieval")
        assert(strcmp(get_mime_type("/path/to/style.css"), "text/css") == 0);
        assert(strcmp(get_mime_type("/path/to/app.js"), "application/javascript") == 0);
        assert(strcmp(get_mime_type("/favicon.ico"), "image/x-icon") == 0);
        assert(strcmp(get_mime_type("/data/unknown"), "application/octet-stream") == 0);
    END_TEST
}


void run_all_tests() {
    test_extract_path();
    test_parse_headers();
    test_mime_type();
}

int main() {
    printf("\n--- Starting HTTP Utility Unit Tests ---\n");
    run_all_tests();
    printf("--- All Tests Passed Successfully ---\n\n");
    return 0;
}
