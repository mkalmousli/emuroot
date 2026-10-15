/* Per-process fake credentials, as fakeroot/proot keep them: the guest sees a
 * consistent identity that set*id calls really change (so a program that
 * drops to an unprivileged user, like apt's _apt, sees the drop take effect),
 * while the real process credentials never change. "Root" here means the fake
 * effective uid is 0. */
#include "internal.h"
#include <errno.h>
#include <string.h>

#define NOID ((uint32_t)-1)

void creds_init(creds_t *c, uint32_t uid, uint32_t gid)
{
    memset(c, 0, sizeof *c);
    for (int i = 0; i < 3; i++) { c->uid[i] = uid; c->gid[i] = gid; }
    c->fsuid = uid;
    c->fsgid = gid;
    c->groups[0] = gid;
    c->ngroups = 1;
}

static int in3(const uint32_t ids[3], uint32_t v) { return v == ids[0] || v == ids[1] || v == ids[2]; }
static int privileged(const creds_t *c) { return c->uid[1] == 0; }

static void apply_uid(creds_t *c, uint32_t r, uint32_t e, uint32_t s)
{
    if (r != NOID) c->uid[0] = r;
    if (e != NOID) c->uid[1] = e;
    if (s != NOID) c->uid[2] = s;
    c->fsuid = c->uid[1];
}

static void apply_gid(creds_t *c, uint32_t r, uint32_t e, uint32_t s)
{
    if (r != NOID) c->gid[0] = r;
    if (e != NOID) c->gid[1] = e;
    if (s != NOID) c->gid[2] = s;
    c->fsgid = c->gid[1];
}

long creds_setuid(creds_t *c, uint32_t u)
{
    if (privileged(c)) apply_uid(c, u, u, u);
    else if (u == c->uid[0] || u == c->uid[2]) apply_uid(c, NOID, u, NOID);
    else return -EPERM;
    return 0;
}

long creds_setgid(creds_t *c, uint32_t g)
{
    if (privileged(c)) apply_gid(c, g, g, g);
    else if (g == c->gid[0] || g == c->gid[2]) apply_gid(c, NOID, g, NOID);
    else return -EPERM;
    return 0;
}

long creds_setresuid(creds_t *c, uint32_t r, uint32_t e, uint32_t s)
{
    if (!privileged(c) && ((r != NOID && !in3(c->uid, r)) || (e != NOID && !in3(c->uid, e)) ||
                           (s != NOID && !in3(c->uid, s))))
        return -EPERM;
    apply_uid(c, r, e, s);
    return 0;
}

long creds_setresgid(creds_t *c, uint32_t r, uint32_t e, uint32_t s)
{
    if (!privileged(c) && ((r != NOID && !in3(c->gid, r)) || (e != NOID && !in3(c->gid, e)) ||
                           (s != NOID && !in3(c->gid, s))))
        return -EPERM;
    apply_gid(c, r, e, s);
    return 0;
}

long creds_setreuid(creds_t *c, uint32_t r, uint32_t e)
{
    if (!privileged(c) && ((r != NOID && r != c->uid[0] && r != c->uid[1]) ||
                           (e != NOID && !in3(c->uid, e))))
        return -EPERM;
    uint32_t old_real = c->uid[0], new_e = e != NOID ? e : c->uid[1];
    int save = r != NOID || (e != NOID && e != old_real);
    apply_uid(c, r, e, save ? new_e : NOID);
    return 0;
}

long creds_setregid(creds_t *c, uint32_t r, uint32_t e)
{
    if (!privileged(c) && ((r != NOID && r != c->gid[0] && r != c->gid[1]) ||
                           (e != NOID && !in3(c->gid, e))))
        return -EPERM;
    uint32_t old_real = c->gid[0], new_e = e != NOID ? e : c->gid[1];
    int save = r != NOID || (e != NOID && e != old_real);
    apply_gid(c, r, e, save ? new_e : NOID);
    return 0;
}

long creds_setgroups(creds_t *c, const uint32_t *list, long n)
{
    if (!privileged(c)) return -EPERM;
    if (n < 0 || n > CREDS_MAX_GROUPS) return -EINVAL;
    memcpy(c->groups, list, (size_t)n * sizeof *list);
    c->ngroups = (int)n;
    return 0;
}

/* setfsuid/setfsgid return the previous value and change it only if allowed. */
long creds_setfsuid(creds_t *c, uint32_t u)
{
    uint32_t old = c->fsuid;
    if (u != NOID && (privileged(c) || u == c->fsuid || in3(c->uid, u))) c->fsuid = u;
    return old;
}

long creds_setfsgid(creds_t *c, uint32_t g)
{
    uint32_t old = c->fsgid;
    if (g != NOID && (privileged(c) || g == c->fsgid || in3(c->gid, g))) c->fsgid = g;
    return old;
}
