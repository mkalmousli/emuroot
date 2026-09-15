"""Renders src/cli/main.c - the emuroot CLI front-end.

Author: Mohamad Almousli. GPL-3.0-only license, see LICENSE.
"""
from generator.spec.project import Project
from generator.banner import c_header

PATH = "src/cli/main.c"

_BODY_TEMPLATE = r"""
#include "emuroot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(const char *prog)
{
    fprintf(stderr,
        "%(name)s - independent user-space fake-root / filesystem emulation\n"
        "Author: %(author)s\n\n"
        "Usage: %%s [options] -- <command> [args...]\n"
        "  -r <path>          set emulated root (like chroot, no privileges needed)\n"
        "  -b <host:guest>    bind a host path onto a guest path (repeatable)\n"
        "  -u <uid:gid>       report this uid/gid to the guest process\n"
        "  -n <sysname:release:machine>  fake uname() output\n"
        "  -v                 verbose syscall trace to stderr\n"
        "  -V                 print version and exit\n"
        "  -h                 show this help\n",
        prog);
}

int main(int argc, char *argv[])
{
    emuroot_ctx_t *ctx = emuroot_create();
    if (!ctx) {
        fprintf(stderr, "%(name)s: failed to allocate context\n");
        return 1;
    }

    int opt;
    while ((opt = getopt(argc, argv, "+r:b:u:n:vVh")) != -1) {
        switch (opt) {
            case 'r':
                emuroot_set_root(ctx, optarg);
                break;
            case 'b': {
                char *sep = strchr(optarg, ':');
                if (!sep) {
                    fprintf(stderr, "%(name)s: -b expects host:guest\n");
                    return 2;
                }
                *sep = '\0';
                emuroot_bind(ctx, optarg, sep + 1);
                break;
            }
            case 'u': {
                unsigned int uid, gid;
                if (sscanf(optarg, "%%u:%%u", &uid, &gid) != 2) {
                    fprintf(stderr, "%(name)s: -u expects uid:gid\n");
                    return 2;
                }
                emuroot_set_fake_ids(ctx, uid, gid);
                break;
            }
            case 'n': {
                char sysname[65] = {0}, release[65] = {0}, machine[65] = {0};
                sscanf(optarg, "%%64[^:]:%%64[^:]:%%64[^:]", sysname, release, machine);
                emuroot_set_fake_uname(ctx, sysname, release, machine);
                break;
            }
            case 'v':
                emuroot_set_verbose(ctx, 1);
                break;
            case 'V': {
                char buf[64];
                emuroot_version_string(buf, sizeof(buf));
                printf("%%s\n", buf);
                return 0;
            }
            case 'h':
            default:
                usage(argv[0]);
                return opt == 'h' ? 0 : 2;
        }
    }

    if (optind >= argc) {
        usage(argv[0]);
        return 2;
    }

    char **cmd = &argv[optind];
    int rc = emuroot_run(ctx, cmd, NULL);
    emuroot_destroy(ctx);

    if (rc < 0) {
        fprintf(stderr, "%(name)s: %%s\n", emuroot_strerror(rc));
        return 1;
    }
    return rc;
}
"""


def render(p: Project) -> str:
    banner = c_header(p, f"{p.name} - command-line front-end for lib{p.name}.", (
        "Usage:\n"
        f"  {p.name} [-r root] [-b host:guest]... [-u uid:gid] [-v] -- cmd [args...]"
    ))
    body = _BODY_TEMPLATE % {"name": p.name, "author": p.author}
    return banner + "\n" + body
