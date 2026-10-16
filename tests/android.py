"""Tests run against a real Android version's userland - see `./test.py --android`
(unpacked system images, in Docker) and `./test.py --adb` (a device or emulator).

They have to run on very different userlands: Android 2.3's shell has no `test`
or `[`, and its toolbox has little more than cat/ls/mkdir/mv/ln/id/rm; Android 6+
has mksh and toybox. So each case is written in the common denominator of POSIX
shell (`case`, not `[`) and lists the commands it needs; the runner probes the
userland and runs only the cases whose commands exist.

Preamble (tests/harness.py, android_preamble): `E` is a function running emuroot
over a fresh fake root with the system's tools bound in and PATH=/system/bin;
`eq A B` fails the case unless A equals B; $R is the fake root, $W a scratch dir
outside it.
"""

# name -> (commands it needs, script)
CASES = {
    "version": ([], """
        case "$($EMUROOT -V)" in "emuroot "*) ;; *) echo "bad version: $($EMUROOT -V)"; exit 1;; esac
    """),

    "exit_status_propagates": ([], """
        E -- sh -c 'exit 7'; eq $? 7
    """),

    "path_reads_are_redirected": (["cat"], """
        echo fake > $R/etc/hostname
        eq "$(E -- cat /etc/hostname)" fake
    """),

    "path_writes_are_redirected": (["cat"], """
        E -- sh -c 'echo written > /tmp/out'
        eq "$(cat $R/tmp/out)" written
    """),

    "the_real_filesystem_is_hidden": (["cat"], """
        echo real > $W/marker
        eq "$(cat $W/marker)" real
        case "$(E -- cat $W/marker 2>/dev/null)" in real) echo "escaped the root"; exit 1;; esac
    """),

    "directories_and_cd": (["mkdir", "ls"], """
        eq "$(E -- sh -c 'mkdir /a && mkdir /a/b && cd /a/b && echo ok')" ok
        eq "$(ls $R/a)" b
    """),

    "rename": (["mv", "ls"], """
        echo data > $R/tmp/one
        E -- mv /tmp/one /tmp/two
        eq "$(ls $R/tmp)" two
        eq "$(cat $R/tmp/two)" data
    """),

    "absolute_symlinks_stay_in_the_root": (["ln", "cat"], """
        echo fake > $R/etc/hostname
        E -- ln -s /etc/hostname /tmp/link
        eq "$(E -- cat /tmp/link)" fake
    """),

    "bindings_remap_paths": (["cat"], """
        echo seen > $W/shared
        eq "$(E -b $W/shared:/mnt/shared -- cat /mnt/shared)" seen
    """),

    "fake_ids_are_reported": (["id"], """
        case "$(E -u 0:0 -- id)" in "uid=0"*"gid=0"*) ;; *) echo "root: $(E -u 0:0 -- id 2>&1)"; exit 1;; esac
        case "$(E -u 2000:2000 -- id)" in "uid=2000"*"gid=2000"*) ;; *) echo "shell: $(E -u 2000:2000 -- id 2>&1) rc=$?"; E -u 2000:2000 -v -- id 2>&1 | while read l; do case "$l" in *sys=1[0-9][0-9]\ *|*uid*|*gid*) echo "  $l";; esac; done; exit 1;; esac
    """),

    "android_account_names": (["id"], """
        # bionic resolves these from its built-in table of Android ids
        case "$(E -u 0:0 -- id)" in "uid=0(root)"*) ;; *) exit 1;; esac
        case "$(E -u 2000:2000 -- id)" in "uid=2000(shell)"*) ;; *) exit 1;; esac
        case "$(E -u 1000:1000 -- id)" in "uid=1000(system)"*) ;; *) exit 1;; esac
    """),

    "pipes_and_children": (["cat"], """
        eq "$(E -- sh -c 'echo through | cat')" through
        set -- $(E -- sh -c 'for i in 1 2 3 4 5 6 7 8; do (echo x) & done; wait')
        eq $# 8
    """),

    "the_guest_sees_the_images_files": (["ls"], """
        case "$(E -- ls /system/bin/sh)" in *sh) ;; *) exit 1;; esac
    """),

    "chown_succeeds_for_the_fake_root": (["chown"], """
        E -u 0:0 -- sh -c 'echo > /tmp/c'
        E -u 0:0 -- chown system /tmp/c
    """),

    # --- what toybox (Android 6+) adds ----------------------------------------

    "uname_is_faked": (["uname"], """
        eq "$(E -n FakeOS:9.9.9:fake64 -- uname -s)" FakeOS
        eq "$(E -n FakeOS:9.9.9:fake64 -- uname -r)" 9.9.9
    """),

    "sed_in_place": (["sed"], """
        echo old > $R/etc/conf
        E -- sed -i s/old/new/ /etc/conf
        eq "$(cat $R/etc/conf)" new
    """),

    "recursive_copy_and_find": (["cp", "find", "mkdir"], """
        E -- sh -c 'mkdir -p /src/d && echo x > /src/d/f && cp -r /src /dst'
        eq "$(cat $R/dst/d/f)" x
        eq "$(E -- find /dst -type f)" /dst/d/f
    """),

    "fake_ownership_is_remembered": (["chown", "stat"], """
        eq "$(E -u 0:0 -- sh -c 'echo > /tmp/o; chown system:radio /tmp/o; stat -c %u:%g /tmp/o')" 1000:1001
    """),
}

ALL_COMMANDS = sorted({c for needs, _ in CASES.values() for c in needs})


def applicable(present: set[str]) -> dict[str, str]:
    """The cases that can run with the commands the userland has."""
    return {name: script for name, (needs, script) in CASES.items() if set(needs) <= present}
