package io.github.mkalmousli.emuroot;

import android.os.ParcelFileDescriptor;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Runs commands inside a fake root filesystem, as a fake user, without root.
 *
 * <pre>{@code
 * try (Emuroot e = new Emuroot.Builder()
 *         .root(new File(getFilesDir(), "rootfs"))
 *         .bind("/system", "/system").bind("/dev", "/dev").bind("/proc", "/proc")
 *         .fakeIds(0, 0)
 *         .build()) {
 *     EmurootProcess p = e.start("/system/bin/sh", "-c", "id; echo hi > /tmp/x");
 *     String out = new String(p.getInputStream().readAllBytes());
 *     int status = p.waitFor();
 * }
 * }</pre>
 *
 * Every {@link #start} runs the guest under its own supervisor process, so it never
 * interferes with the app's other child processes.
 *
 * <p>On Google Play: the guest's programs must be executable by the app's SELinux
 * domain. From Android 10 (targetSdk 29+) that excludes files in the app's data
 * directory, so ship executables as native libraries (see docs/android.md) or use
 * the system's own, as in the example.
 */
public final class Emuroot implements AutoCloseable {
    static {
        System.loadLibrary("emuroot");
    }

    /** The guest is killed when the thread that started it ends, so start it from one that lives
     *  as long as the process: this single daemon thread. */
    private static final ExecutorService SPAWNER = Executors.newSingleThreadExecutor(r -> {
        Thread t = new Thread(r, "emuroot-spawner");
        t.setDaemon(true);
        return t;
    });

    private final long ctx;
    private boolean closed;

    private Emuroot(long ctx) {
        this.ctx = ctx;
    }

    /** A report of what this device lets emuroot use (ptrace, memory access, seccomp, ...). */
    public static String diagnose() {
        return nativeDiagnose();
    }

    /** True if full emulation is available on this device (otherwise emuroot degrades). */
    public static boolean isFullyAvailable() {
        return diagnose().contains("full emulation available");
    }

    public static String version() {
        return nativeVersion();
    }

    /**
     * Starts a command inside the fake root. The command is looked up in the guest's
     * PATH, so give the guest's PATH in {@code env} or use an absolute path.
     *
     * @param env extra/complete environment as {@code KEY=value}; null inherits the app's
     */
    public EmurootProcess start(List<String> command, Map<String, String> env) throws IOException {
        if (command.isEmpty()) throw new IllegalArgumentException("empty command");
        String[] argv = command.toArray(new String[0]);
        String[] envp = null;
        if (env != null) {
            List<String> pairs = new ArrayList<>();
            for (Map.Entry<String, String> e : env.entrySet()) pairs.add(e.getKey() + "=" + e.getValue());
            envp = pairs.toArray(new String[0]);
        }
        final String[] fargv = argv, fenvp = envp;
        synchronized (this) {
            if (closed) throw new IllegalStateException("closed");
        }
        int[] started;
        try {
            started = SPAWNER.submit((Callable<int[]>) () -> nativeStart(ctx, fargv, fenvp)).get();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new IOException("interrupted while starting " + argv[0], e);
        } catch (ExecutionException e) {
            throw new IOException("failed to start " + argv[0], e.getCause());
        }
        if (started == null) throw new IOException("failed to start " + argv[0]);
        return new EmurootProcess(started[0],
                ParcelFileDescriptor.adoptFd(started[1]),
                ParcelFileDescriptor.adoptFd(started[2]),
                ParcelFileDescriptor.adoptFd(started[3]));
    }

    public EmurootProcess start(String... command) throws IOException {
        return start(Arrays.asList(command), null);
    }

    @Override
    public synchronized void close() {
        if (!closed) {
            closed = true;
            nativeDestroy(ctx);
        }
    }

    /** Configures an {@link Emuroot}. */
    public static final class Builder {
        private File root;
        private final List<String[]> binds = new ArrayList<>();
        private boolean fakeIds;
        private int uid, gid;
        private String[] uname;
        private File idsFile;
        private boolean verbose;

        /** The directory that is "/" for the guest. */
        public Builder root(File dir) {
            this.root = dir;
            return this;
        }

        /** Makes {@code hostPath} appear at {@code guestPath} inside the root (later binds win). */
        public Builder bind(String hostPath, String guestPath) {
            binds.add(new String[]{hostPath, guestPath});
            return this;
        }

        /** Makes the guest run as this user (0, 0 for root); files look owned by it. */
        public Builder fakeIds(int uid, int gid) {
            this.fakeIds = true;
            this.uid = uid;
            this.gid = gid;
            return this;
        }

        /** Fakes uname(). */
        public Builder fakeUname(String sysname, String release, String machine) {
            this.uname = new String[]{sysname, release, machine};
            return this;
        }

        /** Keeps fake file ownership (chown) in this file across runs. */
        public Builder idsFile(File file) {
            this.idsFile = file;
            return this;
        }

        /** Logs every translated path to logcat's stderr stream. */
        public Builder verbose(boolean on) {
            this.verbose = on;
            return this;
        }

        public Emuroot build() {
            long ctx = nativeCreate();
            if (ctx == 0) throw new IllegalStateException("out of memory");
            Emuroot e = new Emuroot(ctx);
            try {
                if (root != null) check(nativeSetRoot(ctx, root.getAbsolutePath()), "root");
                for (String[] b : binds) check(nativeBind(ctx, b[0], b[1]), "bind " + b[1]);
                if (fakeIds) check(nativeSetFakeIds(ctx, uid, gid), "fakeIds");
                if (uname != null) check(nativeSetFakeUname(ctx, uname[0], uname[1], uname[2]), "fakeUname");
                if (idsFile != null) check(nativeSetIdsFile(ctx, idsFile.getAbsolutePath()), "idsFile");
                if (verbose) nativeSetVerbose(ctx, true);
            } catch (RuntimeException ex) {
                e.close();
                throw ex;
            }
            return e;
        }

        private static void check(int status, String what) {
            if (status != 0) throw new IllegalArgumentException("emuroot rejected " + what + " (status " + status + ")");
        }
    }

    // --- native (emuroot_jni.c) ---
    private static native long nativeCreate();
    private static native void nativeDestroy(long ctx);
    private static native int nativeSetRoot(long ctx, String path);
    private static native int nativeBind(long ctx, String host, String guest);
    private static native int nativeSetFakeIds(long ctx, int uid, int gid);
    private static native int nativeSetFakeUname(long ctx, String sys, String rel, String machine);
    private static native int nativeSetIdsFile(long ctx, String path);
    private static native int nativeSetVerbose(long ctx, boolean on);
    private static native int[] nativeStart(long ctx, String[] argv, String[] envp);
    static native int nativeWait(int pid);
    static native int nativeTerminate(int pid);
    private static native String nativeDiagnose();
    private static native String nativeVersion();
}
