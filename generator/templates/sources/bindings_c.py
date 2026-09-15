"""Renders src/bindings.c.

Author: Mohamad Almousli. GPL-3.0-only license, see LICENSE.
"""
from generator.spec.project import Project
from generator.banner import c_header

PATH = "src/bindings.c"


def render(p: Project) -> str:
    banner = c_header(p, "bindings.c - manage the host<->guest path binding table.")
    return f"""{banner}
#include "internal.h"
#include <string.h>

int emuroot_bindings_add(struct emuroot_ctx *ctx, const char *host, const char *guest)
{{
    if (!ctx || !host || !guest)
        return EMUROOT_ERR_INVAL;
    if (ctx->nbindings >= EMUROOT_MAX_BINDINGS)
        return EMUROOT_ERR_NOMEM;
    if (strlen(host) >= EMUROOT_PATH_MAX || strlen(guest) >= EMUROOT_PATH_MAX)
        return EMUROOT_ERR_INVAL;

    binding_t *b = &ctx->bindings[ctx->nbindings];
    strncpy(b->host, host, EMUROOT_PATH_MAX - 1);
    b->host[EMUROOT_PATH_MAX - 1] = '\\0';
    strncpy(b->guest, guest, EMUROOT_PATH_MAX - 1);
    b->guest[EMUROOT_PATH_MAX - 1] = '\\0';
    b->guest_len = strlen(b->guest);
    ctx->nbindings++;
    return EMUROOT_OK;
}}
"""
