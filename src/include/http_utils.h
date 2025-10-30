#ifndef HTTP_UTILS_H
#define HTTP_UTILS_H

#include <stddef.h> // For size_t

// --- Data Structures ---
#define MAX_HEADERS 32
#define MAX_HEADER_LEN 256

typedef struct {
    char key[MAX_HEADER_LEN];
    char value[MAX_HEADER_LEN];
} http_header;


// --- Function Declarations ---

/**
 * @brief Extracts the request path (e.g., "/index.html") from the HTTP request line.
 * @return Dynamically allocated string containing the path, or NULL on error.
 */
char* extract_path(const char* request);

/**
 * @brief Parses the HTTP request buffer to extract individual header key-value pairs.
 * @return The number of headers successfully parsed.
 */
int parse_headers(char* request_buffer, http_header headers[], int max_headers);

/**
 * @brief Utility to retrieve a specific header value (case-insensitive key search).
 * @return Pointer to the value string, or NULL if the header is not found.
 */
const char* get_header_value(const http_header headers[], int num_headers, const char* key);

/**
 * @brief Simple utility to determine the MIME type based on file extension.
 * @return MIME type string.
 */
const char *get_mime_type(const char *path);

/**
 * @brief Uses zlib's compress2 to compress the given data into Gzip format.
 * @return Dynamically allocated buffer containing compressed data, or NULL on error.
 */
unsigned char* compress_data_gzip(const unsigned char *data, size_t data_len, size_t *compressed_len);

#endif // HTTP_UTILS_H
