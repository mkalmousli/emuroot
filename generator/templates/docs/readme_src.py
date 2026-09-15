"""Renders the short README.md written alongside the generated C source
(build/<arch>/src/README.md) - just enough to explain what this
directory is and where it comes from.

Author: Mohamad Almousli. GPL-3.0-only license, see LICENSE.
"""
from generator.spec.project import Project

PATH = "src/README.md"

_TEMPLATE = """# %(name)s (generated source, %(arch)s)

%(summary)s

This is generated source, produced for the `%(arch)s` architecture.
Do not hand-edit it - see %(repo)s for the project, build
instructions, and to report issues.

License: %(license)s, see LICENSE in this directory.
"""


def render(p: Project, arch_key: str) -> str:
    return _TEMPLATE % {
        "name": p.name, "arch": arch_key, "summary": p.summary,
        "repo": p.repo, "license": p.license_spdx,
    }
