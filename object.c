int object_write(const char *type, const void *data, size_t size, char *hash_hex) {
    unsigned char hash[SHA256_DIGEST_LENGTH];

    // Create header: "type size\0"
    char header[64];
    int header_len = snprintf(header, sizeof(header), "%s %zu", type, size) + 1;

    // Combine header + data
    size_t total_size = header_len + size;
    unsigned char *buffer = malloc(total_size);
    if (!buffer) return -1;

    memcpy(buffer, header, header_len);
    memcpy(buffer + header_len, data, size);

    // Compute SHA256
    SHA256(buffer, total_size, hash);

    // Convert hash to hex string
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(hash_hex + (i * 2), "%02x", hash[i]);
    }
    hash_hex[64] = '\0';

    // Create directory path
    char dir_path[256];
    snprintf(dir_path, sizeof(dir_path), ".pes/objects/%.2s", hash_hex);

    mkdir(".pes/objects", 0755);
    mkdir(dir_path, 0755);

    // Create file path
    char file_path[256];
    snprintf(file_path, sizeof(file_path), "%s/%s", dir_path, hash_hex + 2);

    // Write to temp file first
    char temp_path[256];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", file_path);

    FILE *fp = fopen(temp_path, "wb");
    if (!fp) {
        free(buffer);
        return -1;
    }

    fwrite(buffer, 1, total_size, fp);
    fclose(fp);

    // Rename temp file → final file
    rename(temp_path, file_path);

    free(buffer);
    return 0;
}
