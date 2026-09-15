"""
architectures.py - single source of truth for each CPU architecture's
ptrace register layout.

This is hand-derived from each architecture's kernel ptrace ABI (it
doesn't come from headers the way syscall numbers do - see
generator/resolve.py for those), so it lives here as a plain data table:
one entry per architecture. generator/templates/headers/arch_h.py turns
one entry into C; generator/resolve.py uses each entry's `detect`
predicate (run against a compiler's real preprocessor macros) to figure
out which entry matches a given target compiler.

To add a new architecture: add one entry to ARCHES. Nothing else in the
generator needs to know about it.

Author: Mohamad Almousli. GPL-3.0-only license, see LICENSE.
"""


def _has(macro):
    return lambda defined, macros: macro in defined


def _riscv64(defined, macros):
    return "__riscv" in defined and macros.get("__riscv_xlen") == "64"


def _loongarch64(defined, macros):
    return "__loongarch64" in defined or ("__loongarch_lp64" in defined and "__loongarch__" in defined)


def _x86(defined, macros):
    return "__i386__" in defined and "__x86_64__" not in defined


# arch key -> {detect, struct, regs, note?}
#   detect(defined_macro_names, macro_values) -> bool
#     matches this entry against a compiler's real predefined macros.
#   struct
#     the emuroot_regs_t field list (a *prefix* of the kernel's real
#     ptrace register struct - see the module docstring in
#     templates/headers/regs_io_h.py for why a prefix is safe).
#   regs
#     logical register role -> C expression reaching it on `r`. The
#     roles used elsewhere in emuroot's C code are SYSNO, RET, ARG1-6, SP.
#   note (optional)
#     prose comment emitted above the struct, for anything non-obvious
#     about this architecture's calling convention.
ARCHES: dict[str, dict] = {
    "x86_64": dict(
        detect=_has("__x86_64__"),
        struct="uint64_t r15, r14, r13, r12, rbp, rbx, r11, r10, r9, r8;\n"
               "    uint64_t rax, rcx, rdx, rsi, rdi, orig_rax, rip, cs, eflags, rsp;",
        regs={"SYSNO": "orig_rax", "RET": "rax", "ARG1": "rdi", "ARG2": "rsi",
              "ARG3": "rdx", "ARG4": "r10", "ARG5": "r8", "ARG6": "r9", "SP": "rsp"},
    ),
    "x86": dict(
        detect=_x86,
        struct="uint32_t ebx, ecx, edx, esi, edi, ebp, eax;\n"
               "    uint32_t xds, xes, xfs, xgs, orig_eax, eip, xcs, eflags, esp, xss;",
        regs={"SYSNO": "orig_eax", "RET": "eax", "ARG1": "ebx", "ARG2": "ecx",
              "ARG3": "edx", "ARG4": "esi", "ARG5": "edi", "ARG6": "ebp", "SP": "esp"},
    ),
    "aarch64": dict(
        detect=_has("__aarch64__"),
        struct="uint64_t regs[31]; uint64_t sp, pc, pstate;",
        regs={"SYSNO": "regs[8]", "RET": "regs[0]", "ARG1": "regs[0]", "ARG2": "regs[1]",
              "ARG3": "regs[2]", "ARG4": "regs[3]", "ARG5": "regs[4]", "ARG6": "regs[5]", "SP": "sp"},
    ),
    "armv7": dict(
        detect=_has("__arm__"),
        note="32-bit ARM (EABI) - e.g. armeabi-v7a Android devices.\n"
             "uregs[0..15] = r0..pc, [16] = cpsr, [17] = orig_r0. The EABI syscall\n"
             "number lives in r7 for the whole syscall (unlike x86's orig_rax, r7\n"
             "isn't clobbered by the return value).",
        struct="uint32_t uregs[18];",
        regs={"SYSNO": "uregs[7]", "RET": "uregs[0]", "ARG1": "uregs[0]", "ARG2": "uregs[1]",
              "ARG3": "uregs[2]", "ARG4": "uregs[3]", "ARG5": "uregs[4]", "ARG6": "uregs[5]",
              "SP": "uregs[13]"},
    ),
    "riscv64": dict(
        detect=_riscv64,
        struct="uint64_t pc, ra, sp, gp, tp, t0, t1, t2, s0, s1;\n"
               "    uint64_t a0, a1, a2, a3, a4, a5, a6, a7;",
        regs={"SYSNO": "a7", "RET": "a0", "ARG1": "a0", "ARG2": "a1", "ARG3": "a2",
              "ARG4": "a3", "ARG5": "a4", "ARG6": "a5", "SP": "sp"},
    ),
    "loongarch64": dict(
        detect=_loongarch64,
        note="LoongArch64: regs[4..11] = a0..a7 in the standard LP64 calling\n"
             "convention (r4..r11 are argument/syscall registers, r3 is sp).",
        struct="uint64_t regs[32]; uint64_t orig_a0;",
        regs={"SYSNO": "regs[11]", "RET": "regs[4]", "ARG1": "regs[4]", "ARG2": "regs[5]",
              "ARG3": "regs[6]", "ARG4": "regs[7]", "ARG5": "regs[8]", "ARG6": "regs[9]", "SP": "regs[3]"},
    ),
    "ppc64le": dict(
        detect=_has("__powerpc64__"),
        struct="uint64_t gpr[16]; /* gpr[0]=sysno, gpr[1]=sp, gpr[3..8]=args/ret */",
        regs={"SYSNO": "gpr[0]", "RET": "gpr[3]", "ARG1": "gpr[3]", "ARG2": "gpr[4]",
              "ARG3": "gpr[5]", "ARG4": "gpr[6]", "ARG5": "gpr[7]", "ARG6": "gpr[8]", "SP": "gpr[1]"},
    ),
    "s390x": dict(
        detect=_has("__s390x__"),
        struct="uint64_t psw_mask, psw_addr;\n"
               "    uint64_t gprs[16]; /* gprs[1]=sysno, gprs[2..6]=args, gprs[2]=ret, gprs[15]=sp */",
        regs={"SYSNO": "gprs[1]", "RET": "gprs[2]", "ARG1": "gprs[2]", "ARG2": "gprs[3]",
              "ARG3": "gprs[4]", "ARG4": "gprs[5]", "ARG5": "gprs[6]", "ARG6": "gprs[7]", "SP": "gprs[15]"},
    ),
}
