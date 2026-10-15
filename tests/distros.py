"""Tests that run real Linux distributions under emuroot as an unprivileged user.

Each distro's root filesystem (see tests/docker/Dockerfile) is entered with
`$ER` = emuroot as fake root. A case is a shell script run natively; it passes
when it exits 0. `needs` lists the tools a distro must have for the case to apply.
Helpers from the preamble (tests/harness.py): $D rootfs dir, $OSID distro id,
$E0 emuroot without a fake identity, $ER with one, eq A B.
"""

DISTROS = {
    # name: (os-release ID, tools the rootfs provides, emuroot build to use)
    "ubuntu-24.04": ("ubuntu", {"os-release", "bash", "dpkg", "apt", "perl", "ldd", "env"}, "native"),
    "ubuntu-22.04": ("ubuntu", {"os-release", "bash", "dpkg", "apt", "perl", "ldd", "env"}, "native"),
    "debian-12":    ("debian", {"os-release", "bash", "dpkg", "apt", "perl", "ldd", "env"}, "native"),
    "alpine-3.20":  ("alpine", {"os-release", "apk", "env"}, "native"),
    "fedora-40":    ("fedora", {"os-release", "bash", "rpm", "dnf", "python3", "ldd", "env"}, "native"),
    "busybox":      ("busybox", set(), "native"),
    # 32-bit userlands, traced by the 32-bit x86 build (the kernel runs both)
    "i386-alpine-3.20": ("alpine", {"os-release", "apk", "env"}, "x86"),
}

# name -> (needs, script)
CASES = {
    "shell_runs": (set(), """
        eq "$($ER -- /bin/sh -c 'echo ok')" ok
    """),

    "os_release": ({"os-release"}, """
        eq "$($ER -- sh -c '. /etc/os-release; echo $ID')" "$OSID"
    """),

    "fake_root_identity": (set(), """
        eq "$($ER -- id -u)" 0
        eq "$($ER -- id -g)" 0
        eq "$($ER -- id -un)" root
        eq "$($E0 -u 1234:5678 -- id -u)" 1234
        eq "$($E0 -u 1234:5678 -- id -g)" 5678
    """),

    "files_look_root_owned": (set(), """
        [ "$(id -u)" != 0 ] || exit 0       # only meaningful for a non-root user
        eq "$($ER -- sh -c 'touch /tmp/o; stat -c %u:%g /tmp/o /etc | tr "\\n" " "')" "0:0 0:0 "
    """),

    "root_ignores_permission_bits": (set(), """
        # real root bypasses mode bits on files it owns; so must the fake one
        [ "$(id -u)" != 0 ] || exit 0
        $ER -- sh -c '
            set -e
            mkdir /tmp/ro && echo secret > /tmp/ro/f && chmod 555 /tmp/ro && chmod 000 /tmp/ro/f
            touch /tmp/ro/new                       # create in a read-only directory
            [ "$(cat /tmp/ro/f)" = secret ]         # read a mode-000 file
            rm /tmp/ro/new /tmp/ro/f                # delete from a read-only directory
        '
    """),

    "chown_succeeds": (set(), """
        $ER -- sh -c 'touch /tmp/c && chown 1234:5678 /tmp/c && chown -R 0:0 /tmp/c'
    """),

    "uname_is_faked": (set(), """
        eq "$($ER -n Plan9:9.9:mips -- uname -srm)" "Plan9 9.9 mips"
    """),

    "cwd_is_guest_view": (set(), """
        eq "$($ER -- sh -c 'pwd')" /
        eq "$($ER -- sh -c 'cd /etc && pwd')" /etc
        eq "$($ER -- sh -c 'cd /etc && cd .. && pwd')" /
    """),

    "dotdot_cannot_escape": (set(), """
        eq "$($ER -- sh -c 'cd /; ls .. | wc -l')" "$($ER -- sh -c 'ls / | wc -l')"
        eq "$($ER -- sh -c 'cat /../../../etc/passwd | head -c 4')" root
        eq "$($ER -- sh -c 'cd /tmp && cat ../../../../etc/passwd | head -c 4')" root
    """),

    "absolute_symlinks_stay_in_root": (set(), """
        # absolute targets must resolve inside the root, whether reached by an
        # absolute path, a relative one, or through a chain
        $ER -- sh -c 'ln -s /etc /tmp/e && ln -s /tmp/e /tmp/e2'
        eq "$($ER -- sh -c 'head -c 4 /tmp/e2/passwd')" root
        eq "$($ER -- sh -c 'cd /tmp && head -c 4 e/passwd')" root
        eq "$($ER -- readlink /tmp/e2)" /tmp/e
    """),

    "shebang_scripts": (set(), """
        $ER -- sh -c 'printf "#!/bin/sh\\necho script-\\$1\\n" > /tmp/s.sh && chmod +x /tmp/s.sh'
        eq "$($ER -- /tmp/s.sh ok)" script-ok
        eq "$($ER -- sh -c 'cd /tmp && ./s.sh rel')" script-rel
    """),

    "shebang_via_env": ({"env"}, """
        $ER -- sh -c 'printf "#!/usr/bin/env sh\\necho env-ok\\n" > /tmp/e.sh && chmod +x /tmp/e.sh'
        eq "$($ER -- /tmp/e.sh)" env-ok
    """),

    "exit_status_and_signals": (set(), """
        $ER -- sh -c 'exit 42'; eq $? 42
        $ER -- sh -c 'kill -TERM $$'; eq $? 143
        $ER -- /nonexistent 2>/dev/null; [ $? -ne 0 ]
    """),

    "pipes_and_redirects": (set(), """
        eq "$($ER -- sh -c 'printf "a\\nc\\nb\\n" | sort | tail -n 1')" c
        eq "$(printf 'x\\ny\\n' | $ER -- wc -l | tr -d ' ')" 2
    """),

    "process_storm": (set(), """
        # 150 concurrent processes
        eq "$($ER -- sh -c 'for i in $(seq 150); do (sleep 1; echo x) & done; wait' | wc -l | tr -d ' ')" 150
    """),

    "many_and_big_files": (set(), """
        $ER -- sh -c 'mkdir /tmp/m; i=0; while [ $i -lt 300 ]; do : > /tmp/m/f$i; i=$((i+1)); done'
        eq "$($ER -- sh -c 'ls /tmp/m | wc -l' | tr -d ' ')" 300
        $ER -- sh -c 'dd if=/dev/zero of=/tmp/big bs=1024 count=20480 2>/dev/null'
        eq "$($ER -- stat -c %s /tmp/big)" 20971520
    """),

    "hardlinks_and_renames": (set(), """
        $ER -- sh -c 'echo a > /tmp/a && ln /tmp/a /tmp/b && mv /tmp/b /tmp/c && mkdir /tmp/d && mv /tmp/c /tmp/d/'
        eq "$($ER -- stat -c %h /tmp/a)" 2
        eq "$($ER -- cat /tmp/d/c)" a
    """),

    "host_sees_guest_writes": (set(), """
        $ER -- sh -c 'echo hostview > /tmp/hv'
        eq "$(cat $D/tmp/hv)" hostview
    """),

    "tar_roundtrip": (set(), """
        $ER -- sh -c 'tar cf /tmp/t.tar /etc/passwd /etc/group /etc/profile 2>/dev/null || tar cf /tmp/t.tar /etc/passwd /etc/group && mkdir /tmp/x && tar xf /tmp/t.tar -C /tmp/x && [ "$(cat /etc/passwd)" = "$(cat /tmp/x/etc/passwd)" ]'
    """),

    "uses_the_distros_own_libc": ({"ldd"}, """
        # the loader and libraries come from the root, not from the host
        $ER -- sh -c 'ldd /bin/sh 2>&1 | grep -q libc'
    """),

    "python_threads_and_sockets": ({"python3"}, """
        $ER -- python3 -c '
import os, socket, threading
p = "/tmp/s.sock"
srv = socket.socket(socket.AF_UNIX); srv.bind(p); srv.listen(1)
def serve():
    c, _ = srv.accept(); c.sendall(b"pong:" + c.recv(8)); c.close()
t = threading.Thread(target=serve); t.start()
cl = socket.socket(socket.AF_UNIX); cl.connect(p); cl.sendall(b"ping")
assert cl.recv(16) == b"pong:ping"; t.join()
assert os.getcwd() == "/" and os.getuid() == 0
'
        [ -S $D/tmp/s.sock ]
    """),

    "credentials_follow_set_id_calls": ({"python3"}, """
        # like apt dropping to its _apt user: the drop must take effect for the
        # guest (and its children) and must not be reversible
        $ER -- python3 -c '
import os
assert (os.getuid(), os.geteuid(), os.getgid(), os.getegid()) == (0, 0, 0, 0)
os.setgroups([5, 6]); assert sorted(os.getgroups()) == [5, 6]
os.setresgid(100, 100, 100); os.setresuid(100, 100, 100)
assert (os.getuid(), os.geteuid(), os.getgid()) == (100, 100, 100)
assert os.getresuid() == (100, 100, 100)
try:
    os.setuid(0); raise SystemExit("regained root")
except PermissionError:
    pass
pid = os.fork()
if pid == 0:
    os._exit(0 if os.getuid() == 100 else 1)
assert os.waitpid(pid, 0)[1] == 0
'
    """),

    "perl_runs": ({"perl"}, """
        eq "$($ER -- perl -e 'print 6*7')" 42
    """),

    "dpkg_build_install_remove": ({"dpkg"}, """
        $ER -- sh -c '
            set -e
            mkdir -p /tmp/pkg/DEBIAN /tmp/pkg/opt/hello
            printf "#!/bin/sh\\necho hello from pkg\\n" > /tmp/pkg/opt/hello/run
            chmod +x /tmp/pkg/opt/hello/run
            printf "Package: hello\\nVersion: 1.0\\nArchitecture: all\\nMaintainer: t <t@t>\\nDescription: t\\n" > /tmp/pkg/DEBIAN/control
            printf "#!/bin/sh\\ntouch /var/tmp/postinst-ran\\n" > /tmp/pkg/DEBIAN/postinst
            chmod 755 /tmp/pkg/DEBIAN/postinst
            dpkg-deb -b /tmp/pkg /tmp/hello.deb >/dev/null
            dpkg -i /tmp/hello.deb >/dev/null
            [ "$(/opt/hello/run)" = "hello from pkg" ]
            [ -e /var/tmp/postinst-ran ]
            dpkg -r hello >/dev/null
            [ ! -e /opt/hello/run ]
        '
    """),

    "package_database_readable": (set(), """
        if [ -d "$D/var/lib/dpkg" ]; then n=$($ER -- sh -c 'dpkg -l | wc -l'); [ "$n" -gt 50 ]
        elif [ -d "$D/lib/apk" ]; then n=$($ER -- sh -c 'apk info | wc -l'); [ "$n" -gt 5 ]
        elif [ -d "$D/var/lib/rpm" ] || [ -d "$D/usr/lib/sysimage/rpm" ]; then n=$($ER -- sh -c 'rpm -qa | wc -l'); [ "$n" -gt 50 ]
        else exit 0; fi
    """),
}

# Cases that need the network (./test.py --net): real package installs.
NET_CASES = {
    "apt_install_and_run": ({"apt"}, """
        $ER -- sh -c '
            set -e
            export DEBIAN_FRONTEND=noninteractive
            apt-get update >/dev/null 2>&1
            apt-get install -y --no-install-recommends jq >/dev/null 2>&1
            echo "{\\"a\\":[1,2]}" | jq -c ".a|add" | grep -qx 3
        '
    """),

    "apt_compile_c_program": ({"apt"}, """
        [ "$OSID" = ubuntu ] || exit 0
        $ER -- sh -c '
            set -e
            export DEBIAN_FRONTEND=noninteractive
            apt-get update >/dev/null 2>&1
            apt-get install -y --no-install-recommends gcc libc6-dev >/dev/null 2>&1
            cd /tmp
            printf "%s\\n" "#include <unistd.h>" "int main(void){ return getuid() == 0 ? 7 : 1; }" > h.c
            gcc -O2 h.c -o h
            rc=0; ./h || rc=$?
            [ $rc -eq 7 ]           # getuid() == 0 inside the fake root
        '
    """),

    "apk_install_and_run": ({"apk"}, """
        $ER -- sh -c '
            set -e
            printf "http://dl-cdn.alpinelinux.org/alpine/v3.20/main\\nhttp://dl-cdn.alpinelinux.org/alpine/v3.20/community\\n" > /etc/apk/repositories
            apk add --no-progress python3 >/dev/null 2>&1
            [ "$(python3 -c "print(6*7)")" = 42 ]
        '
    """),

    "dnf_install_and_run": ({"dnf"}, """
        $ER -- sh -c '
            set -e
            dnf install -y --setopt=sslverify=false jq >/dev/null 2>&1
            echo "{\\"a\\":[1,2]}" | jq -c ".a|add" | grep -qx 3
        '
    """),
}
