// object.c — Content-addressable object store
//
// Every piece of data (file contents, directory listings, commits) is stored
// as an "object" named by its SHA-256 hash. Objects are stored under
// .pes/objects/XX/YYYYYY... where XX is the first two hex characters of the
// hash (directory sharding).

#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/evp.h>

// ─── PROVIDED ────────────────────────────────────────────────────────────────

void hash_to_hex(const ObjectID *id, char *hex_out) {
    for (int i = 0; i < HASH_SIZE; i++) {
        sprintf(hex_out + i * 2, "%02x", id->hash[i]);
    }
    hex_out[HASH_HEX_SIZE] = '\0';
}

int hex_to_hash(const char *hex, ObjectID *id_out) {
    if (strlen(hex) < HASH_HEX_SIZE) return -1;
    for (int i = 0; i < HASH_SIZE; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%2x", &byte) != 1) return -1;
        id_out->hash[i] = (uint8_t)byte;
    }
    return 0;
}

void compute_hash(const void *data, size_t len, ObjectID *id_out) {
    unsigned int hash_len;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, data, len);
    EVP_DigestFinal_ex(ctx, id_out->hash, &hash_len);
    EVP_MD_CTX_free(ctx);
}

void object_path(const ObjectID *id, char *path_out, size_t path_size) {
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);
    snprintf(path_out, path_size, "%s/%.2s/%s", OBJECTS_DIR, hex, hex + 2);
}

int object_exists(const ObjectID *id) {
    char path[512];
    object_path(id, path, sizeof(path));
    return access(path, F_OK) == 0;
}

// ─── IMPLEMENTATION ──────────────────────────────────────────────────────────

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out) {
    const char *type_str;
    if (type == OBJ_BLOB) type_str = "blob";
    else if (type == OBJ_TREE) type_str = "tree";
    else if (type == OBJ_COMMIT) type_str = "commit";
    else return -1;

    // 1. Build the full object: header + data
    char header[64];
    int header_len = sprintf(header, "%s %zu", type_str, len) + 1;
    size_t full_len = header_len + len;
    uint8_t *full_obj = malloc(full_len);
    if (!full_obj) return -1;

    memcpy(full_obj, header, header_len);
    memcpy(full_obj + header_len, data, len);

    // 2. Compute hash
    compute_hash(full_obj, full_len, id_out);

    // 3. Deduplication check
    if (object_exists(id_out)) {
        free(full_obj);
        return 0;
    }

    // 4. Create shard directory
    char path[512], dir_path[512], temp_path[512];
    object_path(id_out, path, sizeof(path));
    
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id_out, hex);
    snprintf(dir_path, sizeof(dir_path), "%s/%.2s", OBJECTS_DIR, hex);
    mkdir(dir_path, 0755);

    // 5. Write to temp file
    snprintf(temp_path, sizeof(temp_path), "%s/tmp_XXXXXX", dir_path);
    int fd = mkstemp(temp_path);
    if (fd < 0) {
        free(full_obj);
        return -1;
    }

    if (write(fd, full_obj, full_len) != (ssize_t)full_len) {
        close(fd);
        unlink(temp_path);
        free(full_obj);
        return -1;
    }

    // 6. fsync file
    fsync(fd);
    close(fd);

    // 7. Atomic rename
    if (rename(temp_path, path) != 0) {
        unlink(temp_path);
        free(full_obj);
        return -1;
    }

    // 8. fsync directory
    int dfd = open(dir_path, O_RDONLY);
    if (dfd >= 0) {
        fsync(dfd);
        close(dfd);
    }

    free(full_obj);
    return 0;
}

int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out) {
    // 1. Build path
    char path[512];
    object_path(id, path, sizeof(path));

    // 2. Open and read file
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *buffer = malloc(fsize);
    if (!buffer) { fclose(f); return -1; }
    fread(buffer, 1, fsize, f);
    fclose(f);

    // 3. Parse header
    uint8_t *null_byte = memchr(buffer, '\0', fsize);
    if (!null_byte) { free(buffer); return -1; }
    
    size_t header_len = (null_byte - buffer) + 1;
    char *header = (char *)buffer;

    if (strncmp(header, "blob", 4) == 0) *type_out = OBJ_BLOB;
    else if (strncmp(header, "tree", 4) == 0) *type_out = OBJ_TREE;
    else if (strncmp(header, "commit", 6) == 0) *type_out = OBJ_COMMIT;
    else { free(buffer); return -1; }

    // 4. Verify integrity
    ObjectID actual_id;
    compute_hash(buffer, fsize, &actual_id);
    if (memcmp(id->hash, actual_id.hash, HASH_SIZE) != 0) {
        free(buffer);
        return -1;
    }

    // 5 & 6. Extract data
    *len_out = fsize - header_len;
    *data_out = malloc(*len_out);
    if (!*data_out) { free(buffer); return -1; }
    memcpy(*data_out, null_byte + 1, *len_out);

    free(buffer);
    return 0;
}
