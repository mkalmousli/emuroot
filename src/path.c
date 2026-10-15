/* Guest path <-> host path translation: root, bindings and symlinks.
 *
 * Symlinks must be resolved here, component by component, in the *guest's*
 * namespace: the kernel would follow an absolute symlink target such as
 * /usr/bin/foo against the real root, escaping the emulated one. */
#include "internal.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MAX_SYMLINK_HOPS 40

static int has_prefix(const char *s, const char *prefix, size_t len)
{
    return strncmp(s, prefix, len) == 0 && (s[len] == '/' || s[len] == '\0');
}

static void pop_component(char *path)
{
    char *slash = strrchr(path, '/');
    if (slash == path) path[1] = '\0'; else *slash = '\0';
}

/* Map a normalized absolute guest path through bindings, then the root.
 * Of the bindings whose guest prefix matches, the longest wins (the latest
 * added, on ties). */
static void map_to_host(const struct emuroot_ctx *ctx, const char *root, const char *guest, char *out, size_t outsz)
{
    const binding_t *best = NULL;
    for (size_t i = ctx->nbindings; i-- > 0;) {
        const binding_t *b = &ctx->bindings[i];
        if ((strcmp(b->guest, "/") == 0 || has_prefix(guest, b->guest, b->guest_len)) &&
            (!best || b->guest_len > best->guest_len))
            best = b;
    }
    if (best) {
        const char *rest = guest + (strcmp(best->guest, "/") == 0 ? 0 : best->guest_len);
        snprintf(out, outsz, "%s%s", strcmp(best->host, "/") == 0 ? "" : best->host, rest);
        if (!out[0]) snprintf(out, outsz, "/");
    } else {
        snprintf(out, outsz, "%s%s", root, guest);
        /* Not named by any binding: it may still lie under a mount point (a
         * chroot'ed process reaches /new/usr as plain /usr). */
        const binding_t *mounted = NULL;
        for (size_t i = ctx->nbindings; i-- > 0;) {
            const binding_t *b = &ctx->bindings[i];
            if (b->mount_len > 1 && has_prefix(out, b->mount, b->mount_len) &&
                (!mounted || b->mount_len > mounted->mount_len))
                mounted = b;
        }
        if (mounted) {
            char moved[2 * PATH_LEN];
            snprintf(moved, sizeof moved, "%s%s", strcmp(mounted->host, "/") == 0 ? "" : mounted->host,
                     out + mounted->mount_len);
            snprintf(out, outsz, "%.*s", (int)outsz - 1, moved);
            if (!out[0]) snprintf(out, outsz, "/");
        }
    }
}

/* Resolve `guest_path` (absolute) to a host path. When follow_last is 0 a
 * symlink in the final component is left for the kernel (lstat, unlink,
 * rename, ...). Returns 1 if the result differs from the input. */
int emuroot_translate_path_ex(const struct emuroot_ctx *ctx, const char *root, const char *guest_path,
                              int follow_last, char *out, size_t outsz, char *guest_out, size_t guest_outsz)
{
    if (guest_out) snprintf(guest_out, guest_outsz, "%s", guest_path);
    if (guest_path[0] != '/' || (!root[0] && !ctx->nbindings)) {
        snprintf(out, outsz, "%s", guest_path);
        return 0;
    }

    char rest[2 * PATH_LEN];        /* components still to walk */
    char cur[PATH_LEN] = "/";       /* guest path resolved so far */
    char host[PATH_LEN];
    snprintf(rest, sizeof rest, "%s", guest_path);
    size_t trailing_slash = guest_path[strlen(guest_path) - 1] == '/';
    int hops = 0;
    char *p = rest;

    for (;;) {
        while (*p == '/') p++;
        if (!*p) break;
        char *end = strchr(p, '/');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        char comp[256];
        if (len >= sizeof comp) len = sizeof comp - 1;
        memcpy(comp, p, len);
        comp[len] = '\0';
        char *next = end ? end : p + strlen(p);
        int is_last = !*next || (strspn(next, "/") == strlen(next) && !trailing_slash);
        p = next;

        if (strcmp(comp, ".") == 0) continue;
        if (strcmp(comp, "..") == 0) {
            pop_component(cur);
            continue;
        }
        size_t curlen = strlen(cur);
        if (curlen + 1 + len >= sizeof cur) break;
        snprintf(cur + curlen, sizeof cur - curlen, "%s%s", curlen > 1 ? "/" : "", comp);

        if (is_last && !follow_last) break;
        map_to_host(ctx, root, cur, host, sizeof host);
        /* /proc symlinks (self, fd/N, ...) are per-process: let the kernel
         * resolve them for the guest rather than for us. */
        if (strncmp(host, "/proc/", 6) == 0) break;

        char target[PATH_LEN];
        ssize_t n = readlink(host, target, sizeof target - 1);
        if (n < 0) continue;                    /* not a symlink (or absent) */
        if (++hops > MAX_SYMLINK_HOPS) break;   /* ELOOP: let the kernel report it */
        target[n] = '\0';

        if (target[0] == '/') strcpy(cur, "/");
        else pop_component(cur);
        /* rest := target + remaining components */
        char tail[2 * PATH_LEN];
        snprintf(tail, sizeof tail, "%s%s", target, p);
        trailing_slash = tail[strlen(tail) - 1] == '/';
        strcpy(rest, tail);
        p = rest;
    }

    /* Anything still unwalked (after a /proc stop or ELOOP) is appended verbatim. */
    map_to_host(ctx, root, cur, host, sizeof host);
    if (guest_out) snprintf(guest_out, guest_outsz, "%s", cur);       /* symlinks resolved, in the guest */
    if (*p) {
        size_t hl = strlen(host);
        snprintf(host + hl, sizeof host - hl, "%s%s", host[hl - 1] == '/' ? "" : "/", p + strspn(p, "/"));
    }
    if (trailing_slash && host[strlen(host) - 1] != '/' && strlen(host) + 1 < sizeof host)
        strcat(host, "/");
    snprintf(out, outsz, "%s", host);
    return strcmp(host, guest_path) != 0;
}

int emuroot_translate_path(const struct emuroot_ctx *ctx, const char *root, const char *guest_path,
                           int follow_last, char *out, size_t outsz)
{
    return emuroot_translate_path_ex(ctx, root, guest_path, follow_last, out, outsz, NULL, 0);
}

/* Inverse mapping for paths the kernel reports back to the guest (getcwd).
 * Returns 1 if `host` lay under the root or a binding. */
int emuroot_host_to_guest(const struct emuroot_ctx *ctx, const char *root, const char *host, char *out, size_t outsz)
{
    const binding_t *best = NULL;
    for (size_t i = 0; i < ctx->nbindings; i++) {
        const binding_t *b = &ctx->bindings[i];
        size_t hl = strlen(b->host);
        if (hl > 1 && has_prefix(host, b->host, hl) && (!best || hl > strlen(best->host)))
            best = b;
    }
    if (best) {
        snprintf(out, outsz, "%s%s", strcmp(best->guest, "/") == 0 ? "" : best->guest,
                 host + strlen(best->host));
    } else if (root[0] && has_prefix(host, root, strlen(root))) {
        snprintf(out, outsz, "%s", host + strlen(root));
    } else {
        return 0;
    }
    if (!out[0]) snprintf(out, outsz, "/");
    return 1;
}
