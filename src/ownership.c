/* The fake file-ownership database, like fakeroot's: chown "succeeds" for the
 * fake root without changing anything on disk, and the new owner is remembered
 * here, keyed by (device, inode), so that stat then reports it. Files created
 * while the guest runs under a different (emulated) identity are recorded
 * too. The table can be saved to a file and reloaded in a later run. */
#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct owner_entry {
    uint64_t dev, ino;
    uint32_t uid, gid;
    int used;
};

struct owner_db {
    struct owner_entry *slots;      /* open addressing, power-of-two size */
    size_t cap, count;
};

static size_t hash_of(uint64_t dev, uint64_t ino)
{
    uint64_t h = (dev * 0x9E3779B97F4A7C15ULL) ^ (ino * 0xC2B2AE3D27D4EB4FULL);
    return (size_t)(h ^ (h >> 29));
}

static struct owner_entry *find_slot(struct owner_db *db, uint64_t dev, uint64_t ino)
{
    size_t mask = db->cap - 1;
    for (size_t i = hash_of(dev, ino) & mask;; i = (i + 1) & mask) {
        struct owner_entry *e = &db->slots[i];
        if (!e->used || (e->dev == dev && e->ino == ino)) return e;
    }
}

int emuroot_owner_lookup(struct emuroot_ctx *ctx, uint64_t dev, uint64_t ino, uint32_t *uid, uint32_t *gid)
{
    struct owner_db *db = ctx->owners;
    if (!db || db->count == 0) return 0;
    struct owner_entry *e = find_slot(db, dev, ino);
    if (!e->used) return 0;
    *uid = e->uid;
    *gid = e->gid;
    return 1;
}

void emuroot_owner_set(struct emuroot_ctx *ctx, uint64_t dev, uint64_t ino, uint32_t uid, uint32_t gid)
{
    struct owner_db *db = ctx->owners;
    if (!db && !(db = ctx->owners = calloc(1, sizeof *db))) return;
    if ((db->count + 1) * 2 > db->cap) {            /* keep the load factor under 1/2 */
        struct owner_entry *old = db->slots;
        size_t old_cap = db->cap;
        db->cap = old_cap ? old_cap * 2 : 1024;
        db->slots = calloc(db->cap, sizeof *db->slots);
        if (!db->slots) { db->slots = old; db->cap = old_cap; return; }
        for (size_t i = 0; i < old_cap; i++)
            if (old[i].used) *find_slot(db, old[i].dev, old[i].ino) = old[i];
        free(old);
    }
    struct owner_entry *e = find_slot(db, dev, ino);
    if (!e->used) db->count++;
    *e = (struct owner_entry){ dev, ino, uid, gid, 1 };
}

/* One "dev ino uid gid" line per entry. */
void emuroot_owners_load(struct emuroot_ctx *ctx, const char *file)
{
    FILE *f = fopen(file, "r");
    if (!f) return;
    unsigned long long dev, ino;
    unsigned uid, gid;
    while (fscanf(f, "%llu %llu %u %u", &dev, &ino, &uid, &gid) == 4)
        emuroot_owner_set(ctx, dev, ino, uid, gid);
    fclose(f);
}

void emuroot_owners_save(struct emuroot_ctx *ctx, const char *file)
{
    struct owner_db *db = ctx->owners;
    if (!db) return;
    FILE *f = fopen(file, "w");
    if (!f) {
        perror("emuroot: saving the ownership database");
        return;
    }
    for (size_t i = 0; i < db->cap; i++)
        if (db->slots[i].used)
            fprintf(f, "%llu %llu %u %u\n", (unsigned long long)db->slots[i].dev,
                    (unsigned long long)db->slots[i].ino, db->slots[i].uid, db->slots[i].gid);
    fclose(f);
}

void emuroot_owners_free(struct emuroot_ctx *ctx)
{
    if (ctx->owners) free(ctx->owners->slots);
    free(ctx->owners);
    ctx->owners = NULL;
}
