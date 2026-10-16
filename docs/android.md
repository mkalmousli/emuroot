# emuroot on Android

emuroot runs on every Android version whose kernel has `ptrace` — in practice all of them —
as a command-line tool and as a native library inside an app.

## Support matrix

| Android (API) | How emuroot runs | Verified by |
|---|---|---|
| 5 – 16+ (21+) | NDK build (bionic, PIE) or the app library (AAR) | real emulators (x86_64; ARM experimental) and each version's real userland in Docker |
| 2.3 – 4.4 (10–19) | static musl build (`x86-musl`, `armv5-musl`); needs only the kernel | emulators (x86) and Android 2.3–4.4 userland in Docker |
| 1.x – 2.2 (1–9) | the same static `armv5-musl` build; kernel fallbacks are in place | by design only: Google ships no images below API 10 |

The engine adapts to the kernel instead of requiring a recent one (`emuroot -D` shows what it found):

| Missing | Fallback |
|---|---|
| seccomp-bpf (Linux < 4.8, or blocked) | trace every syscall (slower) |
| `PTRACE_GETREGSET` (< 2.6.34) | `PTRACE_GETREGS` |
| `PTRACE_O_EXITKILL` (< 3.8) | `PR_SET_PDEATHSIG` plus the tracer killing its tracees |
| `process_vm_readv` (< 3.2) | `/proc/PID/mem`, then `PTRACE_PEEK/POKE` |
| `ptrace` itself (SELinux, sandboxes) | env-only mode (`chdir` + `EMUROOT_*` variables) |

Force any of them with `EMUROOT_NO_SECCOMP`, `EMUROOT_REGS=getregs`, `EMUROOT_NO_EXITKILL`,
`EMUROOT_GUEST_MEM=procmem|peek`, `EMUROOT_BACKEND=envonly` — that is how they are tested.

## The command line

```sh
./build.py --ndk && ./build.py --musl     # NDK (API 21+) and static (any Android) builds
tools/android-deploy.sh                   # picks the right build for the connected device, pushes, runs `emuroot -D`
```

## In an app (Google Play)

The `android/` Gradle project builds an AAR (`:emuroot`) with the engine as a native
library and a small Java API, plus a `:sample` app.

```kotlin
Emuroot.Builder()
    .root(File(filesDir, "rootfs"))
    .bind("/system", "/system").bind("/dev", "/dev").bind("/proc", "/proc")
    .fakeIds(0, 0)
    .build().use { e ->
        val p = e.start("/system/bin/sh", "-c", "id")
        println(p.inputStream.bufferedReader().readText())
        p.waitFor()
    }
```

* `Emuroot.diagnose()` / `isFullyAvailable()` tell you what this device allows.
* Each guest runs under its own *supervisor process* (`emuroot_spawn`), so it never
  disturbs the app's other children, and `destroy()` stops the whole guest tree.
* Build: `cd android && ./gradlew :emuroot:assembleRelease`. The AAR carries `arm64-v8a`,
  `armeabi-v7a`, `x86`, `x86_64`, all 16 KB page aligned (checked by
  `tools/check_elf_alignment.py`, required by Google Play for Android 15+ targets).
* The library needs API 21 (NDK). Below that, ship the static binary and run it with
  `ProcessBuilder`.

### Rules of the platform (not emuroot's)

* **Executables.** An app targeting Android 10+ (API 29) may not `exec` files from its
  data directory (SELinux). Guest programs must therefore be (a) the system's own
  (`/system/bin/sh`, toybox, ...), or (b) shipped as native libraries named `lib*.so`
  under `jniLibs/<abi>/` and run from `applicationInfo.nativeLibraryDir`
  (`android:extractNativeLibs="true"`, and `useLegacyPackaging = true` in Gradle).
  Scripts and data files in the fake root are fine; the *interpreter* has to be allowed.
* **ptrace.** Apps may trace their own children; the AAR's tests (run on emulators in CI)
  verify this on API 21–36. If a vendor's SELinux policy denies it, emuroot degrades to
  env-only mode and `isFullyAvailable()` returns false.
* **Architectures.** A 32-bit emuroot traces 32-bit guests only, and a 64-bit one 64-bit
  guests; ship guests for the ABI the app runs as.
