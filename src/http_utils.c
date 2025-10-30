#include "include/http_utils.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <zlib.h>
#include <strings.h> // For strcasecmp

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
 * @brief Utility to retrieve a specific header value (case-insensitive key search).
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
 * @brief Uses zlib's compress2 to compress the given data into Gzip format.
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
