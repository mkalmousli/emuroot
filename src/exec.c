/* execve rewriting.
 *
 * The kernel loads a binary's ELF interpreter (and a script's #! interpreter)
 * from the *real* filesystem, ignoring the emulated root: a distro's
 * /lib/ld-musl-x86_64.so.1 simply isn't there, and a glibc binary would run
 * on the host's ld.so. So before an execve we look at the file ourselves and,
 * when needed, turn
 *     execve("/bin/ls", {"ls", ...})
 * into
 *     execve("<root>/lib64/ld-linux-x86-64.so.2",
 *            {"/lib64/ld-linux-x86-64.so.2", "--argv0", "ls", "/bin/ls", ...})
 * i.e. we run the root's own dynamic loader on the program, which then loads
 * the root's libraries through the (translated) openat calls. Scripts are
 * expanded the same way, recursively. */
#include "tracee.h"
#include "guest.h"

#include <elf.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_EXEC_ARGS 8192
#define MAX_SHEBANG_DEPTH 5

/* One argv entry: a string we made up, or a pointer into guest memory. */
typedef struct { char *str; unsigned long ptr; } earg_t;

static char *dupstr(const char *s)
{
    char *d = strdup(s);
    if (!d) abort();
    return d;
}

/* Replace the first `drop` argv entries with `count` made-up strings (which
 * are taken over, along with the freeing of any dropped made-up ones). */
static void replace_head(earg_t *args, size_t *nargs, size_t drop, char **strs, size_t count)
{
    for (size_t i = 0; i < drop; i++) free(args[i].str);
    memmove(args + count, args + drop, (*nargs - drop) * sizeof *args);
    for (size_t i = 0; i < count; i++) {
        args[i].str = strs[i];
        args[i].ptr = 0;
    }
    *nargs = *nargs - drop + count;
}

/* The #! interpreter and its single optional argument. */
static int parse_shebang(const char *head, ssize_t n, char **interp, char **arg)
{
    if (n < 3 || head[0] != '#' || head[1] != '!') return 0;
    char line[256];
    size_t len = 0;
    for (ssize_t i = 2; i < n && head[i] != '\n' && len < sizeof line - 1; i++)
        line[len++] = head[i];
    line[len] = '\0';
    char *p = line + strspn(line, " \t");
    if (!*p) return 0;
    size_t tok = strcspn(p, " \t");
    char *rest = p + tok + strspn(p + tok, " \t");
    p[tok] = '\0';
    while (*rest && strchr(" \t\r", rest[strlen(rest) - 1])) rest[strlen(rest) - 1] = '\0';
    *interp = dupstr(p);
    *arg = *rest ? dupstr(rest) : NULL;
    return 1;
}

/* PT_INTERP of a native-endian ELF file, or 0. */
static int elf_interp(int fd, char *interp, size_t size)
{
    unsigned char id[EI_NIDENT];
    if (pread(fd, id, sizeof id, 0) != (ssize_t)sizeof id || memcmp(id, ELFMAG, SELFMAG) != 0)
        return 0;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    if (id[EI_DATA] != ELFDATA2MSB) return 0;
#else
    if (id[EI_DATA] != ELFDATA2LSB) return 0;
#endif
    uint64_t phoff, phentsize, phnum;
    if (id[EI_CLASS] == ELFCLASS64) {
        Elf64_Ehdr h;
        if (pread(fd, &h, sizeof h, 0) != (ssize_t)sizeof h) return 0;
        phoff = h.e_phoff; phentsize = h.e_phentsize; phnum = h.e_phnum;
    } else if (id[EI_CLASS] == ELFCLASS32) {
        Elf32_Ehdr h;
        if (pread(fd, &h, sizeof h, 0) != (ssize_t)sizeof h) return 0;
        phoff = h.e_phoff; phentsize = h.e_phentsize; phnum = h.e_phnum;
    } else {
        return 0;
    }
    for (uint64_t i = 0; i < phnum; i++) {
        uint64_t type, off, filesz;
        off_t at = (off_t)(phoff + i * phentsize);
        if (id[EI_CLASS] == ELFCLASS64) {
            Elf64_Phdr ph;
            if (pread(fd, &ph, sizeof ph, at) != (ssize_t)sizeof ph) return 0;
            type = ph.p_type; off = ph.p_offset; filesz = ph.p_filesz;
        } else {
            Elf32_Phdr ph;
            if (pread(fd, &ph, sizeof ph, at) != (ssize_t)sizeof ph) return 0;
            type = ph.p_type; off = ph.p_offset; filesz = ph.p_filesz;
        }
        if (type == PT_INTERP) {
            if (filesz == 0 || filesz >= size) return 0;
            if (pread(fd, interp, filesz, (off_t)off) != (ssize_t)filesz) return 0;
            interp[filesz] = '\0';
            return 1;
        }
    }
    return 0;
}

/* glibc >= 2.33's ld.so takes --argv0; older glibc and musl don't. */
static int loader_has_argv0(const char *host_path)
{
    static struct { char path[PATH_LEN]; int ok; } cache[8];
    static int used;
    for (int i = 0; i < used; i++)
        if (strcmp(cache[i].path, host_path) == 0) return cache[i].ok;

    int ok = 0, fd = open(host_path, O_RDONLY);
    if (fd >= 0) {
        static char buf[4 << 20];
        ssize_t n = read(fd, buf, sizeof buf);
        ok = n > 0 && memmem(buf, (size_t)n, "--argv0", 7) != NULL;
        close(fd);
    }
    if (used < 8) {
        snprintf(cache[used].path, PATH_LEN, "%s", host_path);
        cache[used++].ok = ok;
    }
    return ok;
}

static void host_path_of(const tracee_t *t, const char *path, char *host, size_t size)
{
    pid_t pid = t->pid;
    if (path[0] == '/') {
        emuroot_translate_path(t->ctx, t->root, path, 1, host, size);
    } else {                                    /* relative to the guest's real cwd */
        char link[64], cwd[PATH_LEN];
        snprintf(link, sizeof link, "/proc/%d/cwd", (int)pid);
        ssize_t n = readlink(link, cwd, sizeof cwd - 1);
        cwd[n > 0 ? n : 0] = '\0';
        size_t a = strlen(cwd), b = strlen(path);
        if (a + b + 2 > size) { host[0] = '\0'; return; }
        memcpy(host, cwd, a);
        host[a] = '/';
        memcpy(host + a + 1, path, b + 1);
    }
}

/* The guest's absolute path for `path` (which may be relative to its cwd). */
static void guest_absolute(tracee_t *t, const char *path, char *out, size_t size)
{
    if (path[0] == '/') {
        snprintf(out, size, "%s", path);
        return;
    }
    char link[64], cwd[PATH_LEN], guest_cwd[PATH_LEN];
    snprintf(link, sizeof link, "/proc/%d/cwd", (int)t->pid);
    ssize_t n = readlink(link, cwd, sizeof cwd - 1);
    cwd[n > 0 ? n : 0] = '\0';
    if (n <= 0 || !emuroot_host_to_guest(t->ctx, t->root, cwd, guest_cwd, sizeof guest_cwd))
        snprintf(guest_cwd, sizeof guest_cwd, "/");
    size_t len = (size_t)snprintf(out, size, "%s", guest_cwd);
    if (len > 1 && len < size) out[len++] = '/';
    if (len < size) snprintf(out + len, size - len, "%.*s", (int)(size - len - 1), path);
}

/* Returns 1 if the execve was rewritten, 0 to leave it to the generic path
 * translation. path_slot/argv_slot are the syscall argument positions. */
int emuroot_rewrite_exec(tracee_t *t, emuroot_regs_t *regs, int path_slot, int argv_slot)
{
    struct emuroot_ctx *ctx = t->ctx;
    pid_t pid = t->pid;
    unsigned long *path_reg = reg_arg(regs, path_slot), *argv_reg = reg_arg(regs, argv_slot);
    char exec_path[PATH_LEN], host[PATH_LEN];
    if (!*path_reg || !*argv_reg || guest_read_str(pid, *path_reg, exec_path, sizeof exec_path) < 0)
        return 0;

    earg_t *args = calloc(MAX_EXEC_ARGS + 16, sizeof *args);
    if (!args) return 0;
    size_t nargs = 0;
    for (; nargs < MAX_EXEC_ARGS; nargs++) {
        unsigned long p = 0;
        if (guest_read(pid, *argv_reg + nargs * sizeof p, &p, sizeof p) != (ssize_t)sizeof p || !p)
            break;
        args[nargs].ptr = p;
    }
    if (nargs == 0) { free(args); return 0; }

    int rewritten = 0;
    for (int depth = 0; depth < MAX_SHEBANG_DEPTH; depth++) {
        host_path_of(t, exec_path, host, sizeof host);
        int fd = open(host, O_RDONLY);
        if (fd < 0) break;                      /* let the kernel report the error */

        char head[256], interp[PATH_LEN], *sb_interp, *sb_arg;
        ssize_t n = pread(fd, head, sizeof head, 0);
        if (parse_shebang(head, n, &sb_interp, &sb_arg)) {
            close(fd);
            /* argv := interp [arg] script argv[1..] */
            char *lead[3] = { sb_interp, sb_arg, dupstr(exec_path) };
            if (!sb_arg) lead[1] = lead[2];
            replace_head(args, &nargs, 1, lead, sb_arg ? 3 : 2);
            snprintf(exec_path, sizeof exec_path, "%s", sb_interp);
            rewritten = 1;
            continue;
        }
        int has_interp = elf_interp(fd, interp, sizeof interp);
        close(fd);
        if (!has_interp) break;                 /* static binary: runs as is */

        char loader_host[PATH_LEN];
        emuroot_translate_path(ctx, t->root, interp, 1, loader_host, sizeof loader_host);
        struct stat as_is, in_root;                     /* the kernel can find it as is if the */
        if (stat(interp, &as_is) == 0 && stat(loader_host, &in_root) == 0 &&   /* same file is */
            as_is.st_dev == in_root.st_dev && as_is.st_ino == in_root.st_ino)  /* at both paths */
            break;

        /* argv := loader [--argv0 argv0] program argv[1..] */
        char *lead[4];
        size_t k = 0;
        lead[k++] = dupstr(interp);
        if (loader_has_argv0(loader_host)) {
            char a0[PATH_LEN] = "";
            if (args[0].str) snprintf(a0, sizeof a0, "%s", args[0].str);
            else guest_read_str(pid, args[0].ptr, a0, sizeof a0);
            lead[k++] = dupstr("--argv0");
            lead[k++] = dupstr(a0);
        }
        lead[k++] = dupstr(exec_path);
        replace_head(args, &nargs, 1, lead, k);
        guest_absolute(t, exec_path, t->pending_exe, sizeof t->pending_exe);
        snprintf(exec_path, sizeof exec_path, "%s", interp);
        snprintf(host, sizeof host, "%s", loader_host);
        rewritten = 1;
        break;
    }

    int result = 0;
    if (rewritten) {
        /* Lay out [path][strings][pointer array] as one block in guest memory. */
        size_t total = strlen(host) + 1 + (nargs + 1) * sizeof(unsigned long) + 32;
        for (size_t i = 0; i < nargs; i++)
            if (args[i].str) total += strlen(args[i].str) + 1;
        char *blob = calloc(1, total);
        size_t *str_off = calloc(nargs + 1, sizeof *str_off);
        unsigned long base = blob && str_off ? emuroot_scratch(t, total) : 0;
        if (base) {
            size_t off = 0, host_off = 0;
            memcpy(blob, host, strlen(host) + 1);
            off = strlen(host) + 1;
            for (size_t i = 0; i < nargs; i++) {
                if (!args[i].str) continue;
                str_off[i] = off;
                memcpy(blob + off, args[i].str, strlen(args[i].str) + 1);
                off += strlen(args[i].str) + 1;
            }
            off = (off + 15) & ~15UL;
            unsigned long *ptrs = (unsigned long *)(blob + off);
            for (size_t i = 0; i < nargs; i++)
                ptrs[i] = args[i].str ? base + str_off[i] : args[i].ptr;
            ptrs[nargs] = 0;
            if (guest_write(pid, base, blob, off + (nargs + 1) * sizeof(unsigned long)) == 0) {
                *path_reg = base + host_off;
                *argv_reg = base + off;
                regs_set(pid, regs);
                result = 1;
                if (ctx->verbose)
                    fprintf(stderr, "[emuroot] exec \"%s\" -> \"%s\" (+%zu args)\n", exec_path, host, nargs);
            }
        }
        if (!result) {                          /* needed a rewrite and couldn't: don't run it unredirected */
            *path_reg = 0;
            regs_set(pid, regs);
            result = -1;
        }
        free(blob);
        free(str_off);
    }
    for (size_t i = 0; i < nargs; i++)
        free(args[i].str);
    free(args);
    return result;
}
