"""
banner.py - renders the license text and the standard /* ... */
file-header comment used atop every generated C file, from the single
`Project` object in generator/spec/project.py.

The license text itself (LICENSE, at the repository root) is the
unmodified, official GNU GPLv3 text from https://www.gnu.org/licenses/ -
the FSF asks that this text never be edited, so license_text() reads it
from disk rather than duplicating it as a Python string literal here.

Author: Mohamad Almousli. GPL-3.0-only license, see LICENSE.
"""
from pathlib import Path

from generator.spec.project import Project

_REPO_ROOT = Path(__file__).resolve().parent.parent
_LICENSE_FILE = _REPO_ROOT / "LICENSE"

_GPL_NOTICE = """This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <https://www.gnu.org/licenses/>."""


def license_text(p: Project) -> str:
    if not p.license_spdx.startswith("GPL-3.0"):
        raise ValueError(f"banner.py only knows how to render GPL-3.0 text, got {p.license_spdx!r}")
    return _LICENSE_FILE.read_text()


def c_header(p: Project, title: str, body: str = "") -> str:
    """The standard /* ... */ banner every generated C/H file starts with:
    a title, optional prose, a copyright + GPL notice, and the author
    line. p.attribution alone (no notice) is used for non-GPL projects."""
    lines = ["/*", f" * {title}"]
    if body:
        lines.append(" *")
        for line in body.strip("\n").splitlines():
            lines.append(f" * {line}".rstrip())
    lines.append(" *")
    lines.append(f" * Copyright (C) {p.year} {p.author}")
    if p.license_spdx.startswith("GPL"):
        lines.append(" *")
        for line in _GPL_NOTICE.splitlines():
            lines.append(f" * {line}".rstrip())
    lines += [" *", f" * {p.attribution}", " */"]
    return "\n".join(lines)
