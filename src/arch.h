/* Per-architecture ptrace register access.
 *
 * emuroot_regs_t is deliberately a *prefix* of the kernel's real register
 * set: PTRACE_GETREGSET/SETREGSET bound every copy to the iovec length, so
 * a prefix is safe everywhere. Each arch maps the logical roles used by the
 * engine (SYSNO, RET, ARG1-6, SP) onto its registers. */
#ifndef EMUROOT_ARCH_H
#define EMUROOT_ARCH_H

#include <linux/audit.h>
#include <stdint.h>
#include <sys/ptrace.h>
#include <sys/uio.h>

#if defined(__x86_64__)
#define EMU_AUDIT_ARCH AUDIT_ARCH_X86_64
typedef struct {
    uint64_t r15, r14, r13, r12, rbp, rbx, r11, r10, r9, r8;
    uint64_t rax, rcx, rdx, rsi, rdi, orig_rax, rip, cs, eflags, rsp;
} emuroot_regs_t;
#define EMU_SYSNO(r) ((r).orig_rax)
#define EMU_RET(r)   ((r).rax)
#define EMU_ARG1(r)  ((r).rdi)
#define EMU_ARG2(r)  ((r).rsi)
#define EMU_ARG3(r)  ((r).rdx)
#define EMU_ARG4(r)  ((r).r10)
#define EMU_ARG5(r)  ((r).r8)
#define EMU_ARG6(r)  ((r).r9)
#define EMU_SP(r)    ((r).rsp)

#elif defined(__i386__)
#define EMU_AUDIT_ARCH AUDIT_ARCH_I386
typedef struct {
    uint32_t ebx, ecx, edx, esi, edi, ebp, eax;
    uint32_t xds, xes, xfs, xgs, orig_eax, eip, xcs, eflags, esp, xss;
} emuroot_regs_t;
#define EMU_SYSNO(r) ((r).orig_eax)
#define EMU_RET(r)   ((r).eax)
#define EMU_ARG1(r)  ((r).ebx)
#define EMU_ARG2(r)  ((r).ecx)
#define EMU_ARG3(r)  ((r).edx)
#define EMU_ARG4(r)  ((r).esi)
#define EMU_ARG5(r)  ((r).edi)
#define EMU_ARG6(r)  ((r).ebp)
#define EMU_SP(r)    ((r).esp)

#elif defined(__aarch64__)
#define EMU_AUDIT_ARCH AUDIT_ARCH_AARCH64
typedef struct { uint64_t regs[31], sp, pc, pstate; } emuroot_regs_t;
#define EMU_SYSNO(r) ((r).regs[8])
#define EMU_RET(r)   ((r).regs[0])
#define EMU_ARG1(r)  ((r).regs[0])
#define EMU_ARG2(r)  ((r).regs[1])
#define EMU_ARG3(r)  ((r).regs[2])
#define EMU_ARG4(r)  ((r).regs[3])
#define EMU_ARG5(r)  ((r).regs[4])
#define EMU_ARG6(r)  ((r).regs[5])
#define EMU_SP(r)    ((r).sp)

#elif defined(__arm__)
#define EMU_AUDIT_ARCH AUDIT_ARCH_ARM
/* EABI: uregs[0..15] = r0..pc, [16] = cpsr, [17] = orig_r0. The syscall
 * number stays in r7 for the whole call. */
typedef struct { uint32_t uregs[18]; } emuroot_regs_t;
#define EMU_SYSNO(r) ((r).uregs[7])
#define EMU_RET(r)   ((r).uregs[0])
#define EMU_ARG1(r)  ((r).uregs[0])
#define EMU_ARG2(r)  ((r).uregs[1])
#define EMU_ARG3(r)  ((r).uregs[2])
#define EMU_ARG4(r)  ((r).uregs[3])
#define EMU_ARG5(r)  ((r).uregs[4])
#define EMU_ARG6(r)  ((r).uregs[5])
#define EMU_SP(r)    ((r).uregs[13])

#elif defined(__riscv) && __riscv_xlen == 64
#define EMU_AUDIT_ARCH (243 /* EM_RISCV */ | 0x80000000 /* 64BIT */ | 0x40000000 /* LE */)
typedef struct {
    uint64_t pc, ra, sp, gp, tp, t0, t1, t2, s0, s1;
    uint64_t a0, a1, a2, a3, a4, a5, a6, a7;
} emuroot_regs_t;
#define EMU_SYSNO(r) ((r).a7)
#define EMU_RET(r)   ((r).a0)
#define EMU_ARG1(r)  ((r).a0)
#define EMU_ARG2(r)  ((r).a1)
#define EMU_ARG3(r)  ((r).a2)
#define EMU_ARG4(r)  ((r).a3)
#define EMU_ARG5(r)  ((r).a4)
#define EMU_ARG6(r)  ((r).a5)
#define EMU_SP(r)    ((r).sp)

#elif defined(__loongarch64) || defined(__loongarch_lp64)
#define EMU_AUDIT_ARCH (258 /* EM_LOONGARCH */ | 0x80000000 | 0x40000000)
/* regs[4..11] = a0..a7, regs[3] = sp. */
typedef struct { uint64_t regs[32], orig_a0; } emuroot_regs_t;
#define EMU_SYSNO(r) ((r).regs[11])
#define EMU_RET(r)   ((r).regs[4])
#define EMU_ARG1(r)  ((r).regs[4])
#define EMU_ARG2(r)  ((r).regs[5])
#define EMU_ARG3(r)  ((r).regs[6])
#define EMU_ARG4(r)  ((r).regs[7])
#define EMU_ARG5(r)  ((r).regs[8])
#define EMU_ARG6(r)  ((r).regs[9])
#define EMU_SP(r)    ((r).regs[3])

#elif defined(__powerpc64__)
#define EMU_AUDIT_ARCH AUDIT_ARCH_PPC64LE
/* gpr[0] = syscall number, gpr[1] = sp, gpr[3..8] = args / return. */
typedef struct { uint64_t gpr[16]; } emuroot_regs_t;
#define EMU_SYSNO(r) ((r).gpr[0])
#define EMU_RET(r)   ((r).gpr[3])
#define EMU_ARG1(r)  ((r).gpr[3])
#define EMU_ARG2(r)  ((r).gpr[4])
#define EMU_ARG3(r)  ((r).gpr[5])
#define EMU_ARG4(r)  ((r).gpr[6])
#define EMU_ARG5(r)  ((r).gpr[7])
#define EMU_ARG6(r)  ((r).gpr[8])
#define EMU_SP(r)    ((r).gpr[1])

#elif defined(__s390x__)
#define EMU_AUDIT_ARCH AUDIT_ARCH_S390X
/* gprs[1] = syscall number, gprs[2..7] = args, gprs[2] = return, gprs[15] = sp. */
typedef struct { uint64_t psw_mask, psw_addr, gprs[16]; } emuroot_regs_t;
#define EMU_SYSNO(r) ((r).gprs[1])
#define EMU_RET(r)   ((r).gprs[2])
#define EMU_ARG1(r)  ((r).gprs[2])
#define EMU_ARG2(r)  ((r).gprs[3])
#define EMU_ARG3(r)  ((r).gprs[4])
#define EMU_ARG4(r)  ((r).gprs[5])
#define EMU_ARG5(r)  ((r).gprs[6])
#define EMU_ARG6(r)  ((r).gprs[7])
#define EMU_SP(r)    ((r).gprs[15])

#else
#error "emuroot: unsupported architecture - add its register layout to src/arch.h"
#endif

/* Syscall argument n (1-6) of a register set. */
static inline unsigned long *reg_arg(emuroot_regs_t *r, int n)
{
    switch (n) {
    case 1: return (unsigned long *)&EMU_ARG1(*r);
    case 2: return (unsigned long *)&EMU_ARG2(*r);
    case 3: return (unsigned long *)&EMU_ARG3(*r);
    case 4: return (unsigned long *)&EMU_ARG4(*r);
    case 5: return (unsigned long *)&EMU_ARG5(*r);
    default: return (unsigned long *)&EMU_ARG6(*r);
    }
}

#define NT_PRSTATUS_ID ((void *)1)

static inline int regs_get(pid_t pid, emuroot_regs_t *regs)
{
    struct iovec iov = { regs, sizeof(*regs) };
    return ptrace(PTRACE_GETREGSET, pid, NT_PRSTATUS_ID, &iov);
}

static inline int regs_set(pid_t pid, emuroot_regs_t *regs)
{
    struct iovec iov = { regs, sizeof(*regs) };
    return ptrace(PTRACE_SETREGSET, pid, NT_PRSTATUS_ID, &iov);
}

#endif
