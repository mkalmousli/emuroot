"""Renders src/arch.h - one architecture's register access and syscall
numbers, both already resolved by the time this runs (see
generator/resolve.py) - so the output has no #ifdef/#elif chain, just
the single target's macros.

The register-layout data this renders lives in
generator/spec/architectures.py, not here.

Author: Mohamad Almousli. GPL-3.0-only license, see LICENSE.
"""
from generator.spec.project import Project
from generator.spec.architectures import ARCHES
from generator.banner import c_header

PATH = "src/arch.h"  # informational; build.py writes this at its own chosen path


def _render_regs(a: dict) -> str:
    lines = []
    if a.get("note"):
        lines.append("/* " + a["note"].replace("\n", "\n * ") + " */")
    lines.append(f"typedef struct {{\n    {a['struct']}\n}} emuroot_regs_t;")
    for logical, expr in a["regs"].items():
        if logical == "RET":
            lines.append(f"#define EMUROOT_REG_RET(r)        ((r).{expr})")
            lines.append(f"#define EMUROOT_REG_SET_RET(r,v)  ((r).{expr} = (v))")
        elif logical == "SYSNO":
            lines.append(f"#define EMUROOT_REG_SYSNO(r)      ((r).{expr})")
        elif logical == "SP":
            lines.append(f"#define EMUROOT_REG_SP(r)         ((r).{expr})")
        else:
            lines.append(f"#define EMUROOT_REG_{logical}(r)       ((r).{expr})")
    return "\n".join(lines)


def render_one(p: Project, arch_key: str, syscalls: dict[str, tuple[int, str]]) -> str:
    """arch.h for exactly one architecture, with that architecture's real
    syscall numbers (from generator.resolve.resolve_syscalls()) baked in."""
    a = ARCHES[arch_key]
    banner = c_header(p, f"arch.h - register access and syscall numbers for {arch_key}.", (
        "Generated for this specific build: the register layout came from the\n"
        f"{arch_key!r} entry in generator/spec/architectures.py; the syscall\n"
        "numbers below were read from this build's own compiler's real kernel\n"
        "headers (see generator/resolve.py), not copied from memory or another\n"
        "project."
    ))

    sc_lines = [
        f"#define {macro} {value}" + (f" /* {name} */" if name != macro[3:] else "")
        for macro, (value, name) in sorted(syscalls.items())
    ]

    return f"""{banner}
#ifndef EMUROOT_ARCH_H
#define EMUROOT_ARCH_H

#include <stdint.h>

{_render_regs(a)}

{chr(10).join(sc_lines)}

#endif /* EMUROOT_ARCH_H */
"""
