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

    # --- edge cases -------------------------------------------------------

    "symlink_loop_fails_fast": """
        $E -- sh -c 'ln -s /tmp/a /tmp/b; ln -s /tmp/b /tmp/a; cat /tmp/a' 2>/dev/null && exit 1
        exit 0
    """,

    "huge_argument_list": """
        eq "$($E -- sh -c 'echo $#' _ $(seq 1 3000))" 3000
    """,

    "failed_exec_does_not_kill_the_guest": """
        eq "$($E -- sh -c '/nonexistent 2>/dev/null; echo alive')" alive
        $E -- sh -c 'printf "#!/no/such/interp\n" > /tmp/f; chmod +x /tmp/f; /tmp/f' 2>/dev/null; [ $? -ne 0 ]
        eq "$($E -- sh -c '/tmp/f 2>/dev/null; echo alive')" alive
    """,

    "background_jobs_do_not_keep_it_waiting": """
        start=$(date +%s)
        $E -- sh -c 'sleep 30 & exit 5'; eq $? 5
        [ $(( $(date +%s) - start )) -lt 10 ]
    """,

    "deleted_working_directory": """
        $E -- sh -c 'mkdir /tmp/d && cd /tmp/d && rmdir /tmp/d; ls >/dev/null 2>&1; echo survived' | grep -q survived
    """,

    "fifo_and_fd_inheritance": """
        $E -- sh -c 'mkfifo /tmp/p; (echo through-fifo > /tmp/p &); cat /tmp/p' | grep -qx through-fifo
        eq "$($E -- sh -c 'exec 3>/tmp/f; echo viafd >&3; exec 3>&-; cat /tmp/f')" viafd
    """,

    "timestamps_can_be_set": """
        $E -- sh -c 'touch /tmp/t && touch -d @86400 /tmp/t'
        eq "$($E -- stat -c %Y /tmp/t)" 86400
    """,

    "readlink_f_and_cd_through_symlinks": """
        $E -- sh -c 'ln -s /etc /tmp/e'
        eq "$($E -- readlink -f /tmp/e)" /etc
        eq "$($E -- sh -c 'cd /tmp/e && pwd -P')" /etc
    """,

    "deep_directory_tree": """
        eq "$($E -- sh -c 'p=; i=0; while [ $i -lt 60 ]; do p=$p/dddddddddd; i=$((i+1)); done; mkdir -p $p && cd $p && pwd | wc -c')" 661
    """,

    "proc_is_the_guests_own": """
        eq "$($E -- sh -c 'cat /proc/self/status | grep -c ^Name')" 1
        eq "$($E -- sh -c 'test -d /proc/$$ && echo ok')" ok
    """,
    "doctor_reports_full_emulation": """
        out=$($EMUROOT -D); [ $? -eq 0 ]
        echo "$out" | grep -q 'full emulation available'
    """,

    "killing_the_tracer_takes_the_guest_down": """
        $E -- sleep 77 & pid=$!
        sleep 1; kill $pid; wait $pid 2>/dev/null
        sleep 1
        for c in /proc/[0-9]*/cmdline; do tr '\\0' ' ' < $c 2>/dev/null; echo; done | grep -q '^sleep 77' && exit 1
        exit 0
    """,
    # --- the embedding API (emuroot_spawn), as a host program with children of its own uses it ---

    "embed_stdio_and_exit_status": """
        [ -x $EMBED ] || exit 0
        $EMBED $R stdio
    """,

    "embed_leaves_the_hosts_other_children_alone": """
        [ -x $EMBED ] || exit 0
        $EMBED $R other-children
    """,

    "embed_terminate": """
        [ -x $EMBED ] || exit 0
        $EMBED $R terminate
    """,

    "embed_survives_hostile_signal_state": """
        [ -x $EMBED ] || exit 0
        $EMBED $R hostile-signals
    """,

    "embed_does_not_leak_descriptors": """
        [ -x $EMBED ] || exit 0
        $EMBED $R fd-hygiene
    """,

    "embed_host_death_takes_the_guest_down": """
        [ -x $EMBED ] || exit 0
        $EMBED $R orphan > /dev/null & pid=$!
        sleep 2; kill -9 $pid; wait $pid 2>/dev/null
        sleep 1
        for c in /proc/[0-9]*/cmdline; do tr '\\0' ' ' < $c 2>/dev/null; echo; done | grep -q '^sleep 78' && exit 1
        exit 0
    """,
}


# Run against the portable build (make PTRACE=0), i.e. what non-Linux Unixes
# get, and against the normal build with EMUROOT_BACKEND=envonly. There is no
# syscall interception here: the guest only gets the environment and a cwd.
ENVONLY_CASES = {
    "envonly_exports_root": """
        eq "$($E -- sh -c 'echo $EMUROOT_ROOT' 2>/dev/null)" "$R"
    """,

    "envonly_starts_in_root": """
        eq "$($E -- sh -c 'pwd -P' 2>/dev/null)" "$R"
    """,

    "envonly_exports_identity_and_bindings": """
        eq "$($EMUROOT -r $R -u 5:6 -b /usr:/usr -- sh -c 'echo $EMUROOT_FAKE_IDS $EMUROOT_BINDINGS' 2>/dev/null)" "5:6 /usr:/usr"
    """,

    "envonly_warns_about_degradation": """
        $E -- true 2>&1 | grep -q 'env-only'
    """,

    "envonly_propagates_exit_status": """
        $E -- sh -c 'exit 9' 2>/dev/null; eq $? 9
    """,
}
