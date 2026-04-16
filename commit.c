#include "commit.h"
#include "tree.h"
#include "index.h"
#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);
int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out);

// ─── HEAD helpers ─────────────────────────────────────────────────────────

int head_read(ObjectID *id_out) {
    FILE *f = fopen(HEAD_FILE, "r");
    if (!f) return -1;

    char line[256];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return -1; }
    fclose(f);
    line[strcspn(line, "\n")] = '\0';

    // Symbolic ref: "ref: refs/heads/main"
    if (strncmp(line, "ref: ", 5) == 0) {
        char ref_path[512];
        snprintf(ref_path, sizeof(ref_path), ".pes/%s", line + 5);
        FILE *rf = fopen(ref_path, "r");
        if (!rf) return -1;
        char hex[HASH_HEX_SIZE + 1];
        if (!fgets(hex, sizeof(hex), rf)) { fclose(rf); return -1; }
        fclose(rf);
        hex[strcspn(hex, "\n")] = '\0';
        if (strlen(hex) < HASH_HEX_SIZE) return -1;
        return hex_to_hash(hex, id_out);
    }

    // Direct hash in HEAD
    if (strlen(line) < HASH_HEX_SIZE) return -1;
    return hex_to_hash(line, id_out);
}

int head_update(const ObjectID *new_commit) {
    // Find where HEAD points
    FILE *f = fopen(HEAD_FILE, "r");
    if (!f) return -1;
    char line[256];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return -1; }
    fclose(f);
    line[strcspn(line, "\n")] = '\0';

    char target[512];
    if (strncmp(line, "ref: ", 5) == 0)
        snprintf(target, sizeof(target), ".pes/%s", line + 5);
    else
        snprintf(target, sizeof(target), "%s", HEAD_FILE);

    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(new_commit, hex);

    char tmp[560];
    snprintf(tmp, sizeof(tmp), "%s.tmp", target);
    FILE *tf = fopen(tmp, "w");
    if (!tf) return -1;
    fprintf(tf, "%s\n", hex);
    fflush(tf);
    fsync(fileno(tf));
    fclose(tf);
    return rename(tmp, target);
}

// ─── Commit serialization ─────────────────────────────────────────────────

int commit_serialize(const Commit *c, void **data_out, size_t *len_out) {
    char tree_hex[HASH_HEX_SIZE + 1];
    hash_to_hex(&c->tree, tree_hex);

    char buf[8192];
    int len = 0;

    len += snprintf(buf + len, sizeof(buf) - len, "tree %s\n", tree_hex);

    if (c->has_parent) {
        char parent_hex[HASH_HEX_SIZE + 1];
        hash_to_hex(&c->parent, parent_hex);
        len += snprintf(buf + len, sizeof(buf) - len, "parent %s\n", parent_hex);
    }

    len += snprintf(buf + len, sizeof(buf) - len,
                    "author %s %llu\n", c->author, (unsigned long long)c->timestamp);
    len += snprintf(buf + len, sizeof(buf) - len, "\n%s\n", c->message);

    *data_out = malloc(len);
    if (!*data_out) return -1;
    memcpy(*data_out, buf, len);
    *len_out = len;
    return 0;
}

int commit_parse(const void *data, size_t len, Commit *c) {
    memset(c, 0, sizeof(*c));
    const char *buf = (const char *)data;
    const char *end = buf + len;
    const char *ptr = buf;

    while (ptr < end) {
        const char *newline = memchr(ptr, '\n', end - ptr);
        if (!newline) break;

        size_t line_len = newline - ptr;

        if (line_len == 0) {
            // blank line — rest is the message
            ptr = newline + 1;
            size_t msg_len = end - ptr;
            if (msg_len > 0 && ptr[msg_len-1] == '\n') msg_len--;
            if (msg_len >= sizeof(c->message)) msg_len = sizeof(c->message) - 1;
            memcpy(c->message, ptr, msg_len);
            c->message[msg_len] = '\0';
            break;
        }

        if (strncmp(ptr, "tree ", 5) == 0) {
            char hex[HASH_HEX_SIZE + 1];
            memcpy(hex, ptr + 5, HASH_HEX_SIZE);
            hex[HASH_HEX_SIZE] = '\0';
            hex_to_hash(hex, &c->tree);
        } else if (strncmp(ptr, "parent ", 7) == 0) {
            char hex[HASH_HEX_SIZE + 1];
            memcpy(hex, ptr + 7, HASH_HEX_SIZE);
            hex[HASH_HEX_SIZE] = '\0';
            hex_to_hash(hex, &c->parent);
            c->has_parent = 1;
        } else if (strncmp(ptr, "author ", 7) == 0) {
            const char *author_start = ptr + 7;
            const char *ts = newline - 1;
            while (ts > author_start && *ts != ' ') ts--;
            c->timestamp = (uint64_t)atoll(ts + 1);
            size_t author_len = ts - author_start;
            if (author_len >= sizeof(c->author)) author_len = sizeof(c->author) - 1;
            memcpy(c->author, author_start, author_len);
            c->author[author_len] = '\0';
        }

        ptr = newline + 1;
    }
    return 0;
}

// ─── Commit create ────────────────────────────────────────────────────────

int commit_create(const char *message, ObjectID *commit_id_out) {
    Commit c;
    memset(&c, 0, sizeof(c));

    if (tree_from_index(&c.tree) != 0) return -1;

    c.has_parent = (head_read(&c.parent) == 0) ? 1 : 0;
    strncpy(c.author, pes_author(), sizeof(c.author) - 1);
    c.timestamp = (uint64_t)time(NULL);
    strncpy(c.message, message, sizeof(c.message) - 1);

    void *data;
    size_t len;
    if (commit_serialize(&c, &data, &len) != 0) return -1;

    ObjectID commit_id;
    if (object_write(OBJ_COMMIT, data, len, &commit_id) != 0) {
        free(data);
        return -1;
    }
    free(data);

    if (head_update(&commit_id) != 0) return -1;
    if (commit_id_out) *commit_id_out = commit_id;
    return 0;
}

// ─── Commit walk ──────────────────────────────────────────────────────────

int commit_walk(commit_walk_fn callback, void *ctx) {
    ObjectID id;
    if (head_read(&id) != 0) {
        printf("No commits yet.\n");
        return 0;
    }

    while (1) {
        ObjectType type;
        void *data;
        size_t len;
        if (object_read(&id, &type, &data, &len) != 0) return -1;

        Commit c;
        commit_parse(data, len, &c);
        free(data);

        callback(&id, &c, ctx);

        if (!c.has_parent) break;
        id = c.parent;
    }
    return 0;
}
