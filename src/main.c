/* emuroot command-line front-end. */
#include "emuroot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(const char *prog)
{
    fprintf(stderr,
        "emuroot - independent user-space fake-root / filesystem emulation\n"
        "Author: Mohamad Almousli\n\n"
        "Usage: %s [options] -- <command> [args...]\n"
        "  -r <path>          set emulated root (like chroot, no privileges needed)\n"
        "  -b <host:guest>    bind a host path onto a guest path (repeatable)\n"
        "  -u <uid:gid>       report this uid/gid to the guest process\n"
        "  -n <sysname:release:machine>  fake uname() output\n"
        "  -s <file>          keep fake file ownership (chown) in <file> across runs\n"
        "  -v                 verbose syscall trace to stderr\n"
        "  -V                 print version and exit\n"
        "  -h                 show this help\n",
        prog);
}

int main(int argc, char *argv[])
{
    emuroot_ctx_t *ctx = emuroot_create();
    if (!ctx) {
        fprintf(stderr, "emuroot: failed to allocate context\n");
        return 1;
    }

    int opt;
    while ((opt = getopt(argc, argv, "+r:b:u:n:s:vVh")) != -1) {
        switch (opt) {
            case 'r':
                emuroot_set_root(ctx, optarg);
                break;
            case 'b': {
                char *sep = strchr(optarg, ':');
                if (!sep) {
                    fprintf(stderr, "emuroot: -b expects host:guest\n");
                    return 2;
                }
                *sep = '\0';
                emuroot_bind(ctx, optarg, sep + 1);
                break;
            }
            case 'u': {
                unsigned int uid, gid;
                if (sscanf(optarg, "%u:%u", &uid, &gid) != 2) {
                    fprintf(stderr, "emuroot: -u expects uid:gid\n");
                    return 2;
                }
                emuroot_set_fake_ids(ctx, uid, gid);
                break;
            }
            case 'n': {
                char sysname[65] = {0}, release[65] = {0}, machine[65] = {0};
                sscanf(optarg, "%64[^:]:%64[^:]:%64[^:]", sysname, release, machine);
                emuroot_set_fake_uname(ctx, sysname, release, machine);
                break;
            }
            case 's':
                emuroot_set_ids_file(ctx, optarg);
                break;
            case 'v':
                emuroot_set_verbose(ctx, 1);
                break;
            case 'V': {
                char buf[64];
                emuroot_version_string(buf, sizeof(buf));
                printf("%s\n", buf);
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
        fprintf(stderr, "emuroot: %s\n", emuroot_strerror(rc));
        return 1;
    }
    return rc;
}
