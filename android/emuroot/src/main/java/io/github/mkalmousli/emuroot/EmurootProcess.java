package io.github.mkalmousli.emuroot;

import android.os.ParcelFileDescriptor;

import java.io.Closeable;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/** A command running inside a fake root, much like {@link Process}. */
public final class EmurootProcess implements Closeable {
    private final int pid;
    private final ParcelFileDescriptor stdin, stdout, stderr;
    private volatile Integer exitValue;

    EmurootProcess(int pid, ParcelFileDescriptor stdin, ParcelFileDescriptor stdout, ParcelFileDescriptor stderr) {
        this.pid = pid;
        this.stdin = stdin;
        this.stdout = stdout;
        this.stderr = stderr;
    }

    /** The supervisor process's pid (the one to signal; it takes the guest down with it). */
    public int pid() {
        return pid;
    }

    /** Feeds the guest's standard input. */
    public OutputStream getOutputStream() {
        return new ParcelFileDescriptor.AutoCloseOutputStream(stdin);
    }

    /** The guest's standard output. */
    public InputStream getInputStream() {
        return new ParcelFileDescriptor.AutoCloseInputStream(stdout);
    }

    /** The guest's standard error. */
    public InputStream getErrorStream() {
        return new ParcelFileDescriptor.AutoCloseInputStream(stderr);
    }

    /** Waits for the guest to finish; returns its exit status (128+N if it died of signal N). */
    public synchronized int waitFor() {
        if (exitValue == null) exitValue = Emuroot.nativeWait(pid);
        return exitValue;
    }

    /** Stops the guest and everything it started. */
    public void destroy() {
        if (exitValue == null) Emuroot.nativeTerminate(pid);
    }

    @Override
    public void close() throws IOException {
        destroy();
        stdin.close();
        stdout.close();
        stderr.close();
    }
}
