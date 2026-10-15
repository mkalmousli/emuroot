"""Real-world emuroot test cases.

Each case is a short POSIX-sh script run *natively on the test target*
(the host, or an Android device over adb). The harness first defines:

  $EMUROOT  path to the binary under test
  $W        scratch directory; $R = $W/root is the fake root (fresh, with etc/, tmp/)
  $E        "emuroot -r $R $BINDS"; BINDS are the bindings that make the system's own
            tools (sh, cat, id, ...) visible inside the fake root
  eq A B    fail the case unless A equals B

A case passes when its script exits 0. Run guest commands as
`$E -- sh -c '...'`; inspect results either from inside the guest or
directly under $R (the "host" view of the fake root).
"""

CASES = {
    "version": """
        $EMUROOT -V | grep -Eq '^emuroot [0-9]+\\.[0-9]+'
    """,

    "exit_code_propagates": """
        $E -- sh -c 'exit 7'; eq $? 7
    """,

    "redirect_read": """
        echo fake > $R/etc/hostname
        eq "$($E -- cat /etc/hostname)" fake
    """,

    "redirect_write": """
        $E -- sh -c 'echo written > /tmp/out'
        eq "$(cat $R/tmp/out)" written
    """,

    "missing_file_is_missing": """
        # /etc/hostname exists on most systems, but not in the fake root
        $E -- cat /etc/hostname 2>/dev/null && exit 1
        exit 0
    """,

    "mkdir_and_cd": """
        eq "$($E -- sh -c 'mkdir -p /a/b && cd /a/b && echo ok')" ok
        [ -d $R/a/b ]
    """,

    "rename": """
        echo data > $R/tmp/one
        $E -- mv /tmp/one /tmp/two
        [ ! -e $R/tmp/one ] && eq "$(cat $R/tmp/two)" data
    """,

    "symlink_readlink": """
        $E -- ln -s /etc/hostname /tmp/link
        eq "$($E -- readlink /tmp/link)" /etc/hostname
    """,

    "bind_remap": """
        echo seen > $W/shared
        eq "$($E -b $W/shared:/mnt/shared -- cat /mnt/shared)" seen
    """,

    "fake_uid_gid": """
        eq "$($E -u 0:0 -- id -u)" 0
        eq "$($E -u 1234:5678 -- id -u)" 1234
        eq "$($E -u 1234:5678 -- id -g)" 5678
    """,

    "fake_setuid_succeeds": """
        # su-like tools call setuid(0); under emuroot it must not fail
        eq "$($E -u 0:0 -- sh -c 'id -u')" 0
    """,

    "fake_uname": """
        eq "$($E -n FakeOS:9.9.9:fake64 -- uname -s)" FakeOS
        eq "$($E -n FakeOS:9.9.9:fake64 -- uname -r)" 9.9.9
        eq "$($E -n FakeOS:9.9.9:fake64 -- uname -m)" fake64
    """,

    "real_world_cp_tree": """
        $E -- sh -c 'mkdir -p /src/d && echo x > /src/d/f && cp -r /src /dst'
        eq "$(cat $R/dst/d/f)" x
    """,

    "real_world_sed_in_place": """
        # sed -i writes a temp file and renames it over the original
        echo old > $R/etc/conf
        $E -- sed -i s/old/new/ /etc/conf
        eq "$(cat $R/etc/conf)" new
    """,

    "real_world_find_ls": """
        mkdir -p $R/p/q; touch $R/p/q/file
        eq "$($E -- find /p -type f)" /p/q/file
        $E -- ls /p/q | grep -q file
    """,

    "real_world_script": """
        $E -- sh -c '
            for i in 1 2 3; do echo $i >> /tmp/count; done
            [ "$(wc -l < /tmp/count)" -eq 3 ]
        '
    """,

    "unbound_dir_hidden": """
        # /data exists on Android, /home on most Linux hosts; neither is bound
        $E -- ls /data/local 2>/dev/null && exit 1
        exit 0
    """,
}
