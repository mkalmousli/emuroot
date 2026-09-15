"""
resolve.py - asks a target compiler for its own real predefined macros
and kernel syscall numbers, so build.py can generate C source with the
correct architecture and syscall numbers already baked in.

Why this exists: numeric syscall numbers differ per architecture, and
for a handful of syscalls also depend on whether the target uses the
legacy 16-bit-uid ABI (e.g. 32-bit x86/ARM actually call `getresuid32`,
not `getresuid`). Hand-transcribing those numbers is exactly the kind of
boilerplate that silently rots or gets a number wrong. Instead this
module asks the *actual compiler about to build emuroot* to preprocess
<asm/unistd.h> and reads the real numbers straight out of it - there is
nothing to keep in sync by hand.

Author: Mohamad Almousli. GPL-3.0-only license, see LICENSE.
"""
import re
import subprocess

from generator.spec.architectures import ARCHES
from generator.spec.syscalls import SYSCALL_CANDIDATES

_DEFINE_RE = re.compile(r"#define\s+(\S+)\s*(.*)")
_NR_REF_RE = re.compile(r"__NR\w+")


def dump_macros(cc: str) -> dict[str, str]:
    """Ask the compiler to preprocess <asm/unistd.h> and return every
    predefined macro (architecture macros like __x86_64__ *and* every
    __NR_* syscall number macro) as {name: raw_value_text}."""
    out = subprocess.run(
        [cc, "-E", "-dM", "-x", "c", "-"],
        input="#include <asm/unistd.h>\n", capture_output=True, text=True,
    )
    raw: dict[str, str] = {}
    for line in out.stdout.splitlines():
        m = _DEFINE_RE.match(line.strip())
        if m:
            raw[m.group(1)] = m.group(2).strip()
    return raw


def resolve_int(raw: dict[str, str], name: str, seen: set | None = None):
    """Numerically evaluate a __NR_* macro, recursively substituting any
    other __NR_* macros it references (kernel headers commonly alias one
    syscall name to another, e.g. __NR_newfstatat -> __NR3264_fstatat)."""
    seen = seen or set()
    if name in seen or name not in raw:
        return None
    seen.add(name)

    def sub(m: re.Match) -> str:
        resolved = resolve_int(raw, m.group(0), seen)
        return m.group(0) if resolved is None else str(resolved)

    expr = _NR_REF_RE.sub(sub, raw[name])
    try:
        return eval(expr, {"__builtins__": {}})
    except Exception:
        return None


def resolve_syscalls(macros: dict[str, str]) -> dict[str, tuple[int, str]]:
    """{emuroot SC_ macro: (number, kernel-syscall-name-it-came-from)},
    for every syscall this compiler's headers actually define."""
    result = {}
    for macro, candidates in SYSCALL_CANDIDATES.items():
        for name in candidates:
            val = resolve_int(macros, "__NR_" + name)
            if val is not None:
                result[macro] = (val, name)
                break
    return result


def detect_arch(macros: dict[str, str]) -> str | None:
    """Match this compiler's predefined macros against the register-layout
    table in generator/spec/architectures.py, returning that arch's key
    (e.g. "x86_64") or None if unrecognized."""
    defined = set(macros)
    for key, entry in ARCHES.items():
        if entry["detect"](defined, macros):
            return key
    return None
