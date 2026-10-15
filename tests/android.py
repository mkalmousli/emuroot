"""Tests run against a real Android version's userland (bionic libc, linker,
toybox, mksh) — see tools/android_image.py and `./test.py --android`.

They run with the NDK-built (bionic, dynamically linked) emuroot, exactly as it
ships for phones: /system and /apex are the image's, at their real paths. Cases
only use commands every Android since 7 has (mksh + toybox). The preamble is in
tests/harness.py (android_preamble); `$E` is emuroot over a fresh fake root with
the system's tools bound in, and the guest's PATH is /system/bin.
"""
from tests.cases import CASES as BASIC_CASES

# The portable basic cases apply to Android unchanged...
SHARED = ["version", "exit_code_propagates", "redirect_read", "redirect_write",
          "missing_file_is_missing", "mkdir_and_cd", "rename", "symlink_readlink",
          "bind_remap", "fake_setuid_succeeds", "fake_uname",
          "real_world_cp_tree", "real_world_find_ls", "real_world_script",
          "unbound_dir_hidden"]

CASES = {name: BASIC_CASES[name] for name in SHARED}

# ... plus what is specific to Android.
CASES.update({
    "guest_runs_on_bionic": """
        # the guest is the image's own mksh + toybox, loaded by the image's linker
        eq "$($E -- sh -c 'echo $0')" sh
        $E -- toybox true
    """,

    "android_user_names": """
        # bionic resolves these from its built-in table of Android ids
        eq "$($E -u 0:0 -- id -un)" root
        eq "$($E -u 2000:2000 -- id -un)" shell
        eq "$($E -u 1000:1000 -- id -un)" system
        eq "$($E -u 2000:2000 -- id -u)" 2000
    """,

    "fake_ids_are_reported": """
        # toybox's `id` only accepts ids bionic knows, hence Android's own
        eq "$($E -u 2000:2000 -- id -g)" 2000
        eq "$($E -u 1000:2000 -- id -u)" 1000
    """,

    "sed_in_place": """
        echo old > $R/etc/conf
        $E -- sed -i s/old/new/ /etc/conf
        eq "$(cat $R/etc/conf)" new
    """,

    "processes_and_pipes": """
        eq "$($E -- sh -c 'echo b; echo a' | $E -- sort | head -n 1)" a
        eq "$($E -- sh -c 'for i in 1 2 3 4 5 6 7 8; do (echo $i) & done; wait' | wc -l | tr -d ' ')" 8
    """,

    "sees_the_images_files": """
        # /system is the Android image's real tree
        $E -- ls /system/bin/sh /system/bin/toybox /system/lib64/libc.so
    """,
})
