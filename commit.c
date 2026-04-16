// commit.c — Commit creation and simple log

#include "commit.h"
#include "tree.h"
#include "index.h"
#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// object functions
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);
int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out);

// ─── SIMPLE COMMIT CREATE ─────────────────────────────────

int commit_create(const char *message, ObjectID *commit_id_out) {
    ObjectID tree_id;
    if (tree_from_index(&tree_id) != 0) return -1;

    char tree_hex[HASH_HEX_SIZE + 1];
    hash_to_hex(&tree_id, tree_hex);

    char buffer[2048];
    int len = snprintf(buffer, sizeof(buffer),
                       "tree %s\n\n%s\n",
                       tree_hex,
                       message);

    ObjectID commit_id;
    if (object_write(OBJ_COMMIT, buffer, len, &commit_id) != 0)
        return -1;

    // write HEAD directly
    FILE *f = fopen(HEAD_FILE, "w");
    if (!f) return -1;

    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(&commit_id, hex);
    fprintf(f, "%s\n", hex);
    fclose(f);

    if (commit_id_out) *commit_id_out = commit_id;

    return 0;
}

// ─── SIMPLE COMMIT WALK (for log) ─────────────────────────

typedef struct {
    int printed;
} LogCtx;

static void log_callback(const ObjectID *id, const Commit *c, void *ctx) {
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);

    printf("commit %s\n", hex);
    printf("    %s\n", c->message);
}

// minimal commit parser
static int simple_parse(const void *data, Commit *c) {
    memset(c, 0, sizeof(*c));

    char *msg = strstr((char *)data, "\n\n");
    if (msg) {
        msg += 2;
        strncpy(c->message, msg, sizeof(c->message) - 1);
    }
    return 0;
}

int commit_walk(commit_walk_fn callback, void *ctx) {
    FILE *f = fopen(HEAD_FILE, "r");
    if (!f) {
        printf("No commits yet.\n");
        return 0;
    }

    char hex[HASH_HEX_SIZE + 1];
    if (!fgets(hex, sizeof(hex), f)) {
        fclose(f);
        return -1;
    }
    fclose(f);

    hex[strcspn(hex, "\n")] = '\0';

    ObjectID id;
    if (hex_to_hash(hex, &id) != 0) return -1;

    ObjectType type;
    void *data;
    size_t len;

    if (object_read(&id, &type, &data, &len) != 0) return -1;

    Commit c;
    simple_parse(data, &c);

    callback(&id, &c, ctx);

    free(data);
    return 0;
}
