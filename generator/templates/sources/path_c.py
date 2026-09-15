"""Renders src/path.c - guest path -> host path translation.

Author: Mohamad Almousli. GPL-3.0-only license, see LICENSE.
"""
from generator.spec.project import Project
from generator.banner import c_header

PATH = "src/path.c"

_BODY = r"""
#include "internal.h"
#include <string.h>
#include <stdio.h>

/* Collapse "." / ".." / duplicate slashes from an absolute path in-place
 * semantics, writing into out. Does NOT touch the filesystem (no symlink
 * resolution) - callers that need real symlink semantics rely on the
 * host kernel resolving the final translated path itself. */
static void normalize_abs(const char *in, char *out, size_t outsz)
{
    const char *p = in;
    char comps[EMUROOT_MAX_BINDINGS * 2][256];
    int ncomp = 0;
    char comp[256];
    size_t ci = 0;

    while (*p) {
        if (*p == '/') { p++; continue; }
        ci = 0;
        while (*p && *p != '/' && ci < sizeof(comp) - 1)
            comp[ci++] = *p++;
        comp[ci] = '\0';
        while (*p && *p != '/') p++; /* guard overlong components */

        if (strcmp(comp, ".") == 0 || comp[0] == '\0') {
            continue;
        } else if (strcmp(comp, "..") == 0) {
            if (ncomp > 0) ncomp--;
        } else {
            if ((size_t)ncomp < sizeof(comps) / sizeof(comps[0])) {
                strncpy(comps[ncomp], comp, sizeof(comps[0]) - 1);
                comps[ncomp][sizeof(comps[0]) - 1] = '\0';
                ncomp++;
            }
        }
    }

    size_t pos = 0;
    if (outsz > 0) out[0] = '\0';
    for (int i = 0; i < ncomp; i++) {
        int n = snprintf(out + pos, outsz > pos ? outsz - pos : 0, "/%s", comps[i]);
        if (n < 0) break;
        pos += (size_t)n;
        if (pos >= outsz) break;
    }
    if (ncomp == 0 && outsz > 0) {
        out[0] = '/';
        out[1] = '\0';
    }
}

int emuroot_translate_path(const struct emuroot_ctx *ctx, const char *cwd,
                            const char *guest_path, char *out, size_t outsz)
{
    if (!ctx || !guest_path || !out || outsz == 0)
        return -1;

    char abs_path[EMUROOT_PATH_MAX * 2];
    if (guest_path[0] == '/') {
        snprintf(abs_path, sizeof(abs_path), "%s", guest_path);
    } else {
        const char *base = (cwd && cwd[0]) ? cwd : "/";
        snprintf(abs_path, sizeof(abs_path), "%s/%s", base, guest_path);
    }

    char norm[EMUROOT_PATH_MAX];
    normalize_abs(abs_path, norm, sizeof(norm));

    /* Most recently added binding with the longest matching guest prefix
     * wins, mirroring proot-style "last bind shadows earlier ones". */
    const binding_t *best = NULL;
    size_t best_len = 0;
    for (size_t i = ctx->nbindings; i-- > 0;) {
        const binding_t *b = &ctx->bindings[i];
        size_t glen = b->guest_len;
        if (glen == 0) continue;
        int matches = 0;
        if (strcmp(b->guest, "/") == 0) {
            matches = 1;
            glen = 1;
        } else if (strncmp(norm, b->guest, glen) == 0 &&
                   (norm[glen] == '/' || norm[glen] == '\0')) {
            matches = 1;
        }
        if (matches && glen >= best_len) {
            best = b;
            best_len = glen;
        }
    }

    if (best) {
        const char *rest = norm + best_len;
        if (*rest == '/') rest++;
        if (*rest)
            snprintf(out, outsz, "%s/%s", best->host, rest);
        else
            snprintf(out, outsz, "%s", best->host);
        return 1;
    }

    if (ctx->root[0] != '\0') {
        snprintf(out, outsz, "%s%s", ctx->root, norm);
        return 1;
    }

    snprintf(out, outsz, "%s", norm);
    return 0;
}
"""


def render(p: Project) -> str:
    banner = c_header(p, "path.c - guest path -> host path translation.", (
        "This is the heart of emuroot's filesystem emulation. It is a clean-room\n"
        "implementation: given a set of (host, guest) bindings and an optional\n"
        "emulated root, it rewrites whatever path a guest process asks the kernel\n"
        "to resolve into the real host path that should actually be touched."
    ))
    return banner + "\n" + _BODY
