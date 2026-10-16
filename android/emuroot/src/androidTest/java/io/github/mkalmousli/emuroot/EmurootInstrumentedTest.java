package io.github.mkalmousli.emuroot;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import android.content.Context;

import androidx.test.platform.app.InstrumentationRegistry;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Runs the library the way a Google Play app does: inside the app's own process and SELinux
 * domain, with a fake root in its private storage, using only the system's own programs
 * (an app targeting Android 10+ may not execute files from its data directory).
 */
public class EmurootInstrumentedTest {
    private Context context;
    private File root;
    private Emuroot emuroot;
    private final Map<String, String> env = new HashMap<>();

    @Before
    public void setUp() {
        context = InstrumentationRegistry.getInstrumentation().getTargetContext();
        root = new File(context.getCacheDir(), "emuroot-test-root");
        deleteTree(root);
        new File(root, "etc").mkdirs();
        new File(root, "tmp").mkdirs();
        env.put("PATH", "/system/bin:/system/xbin");
        emuroot = new Emuroot.Builder()
                .root(root)
                .bind("/system", "/system").bind("/system_ext", "/system_ext").bind("/product", "/product")
                .bind("/vendor", "/vendor").bind("/odm", "/odm").bind("/apex", "/apex")
                .bind("/linkerconfig", "/linkerconfig").bind("/dev", "/dev").bind("/proc", "/proc")
                .fakeIds(0, 0)
                .build();
    }

    @After
    public void tearDown() {
        emuroot.close();
        deleteTree(root);
    }

    // --- the device ---------------------------------------------------------------------------

    @Test
    public void fullEmulationIsAvailableInsideAnApp() {
        String report = Emuroot.diagnose();
        android.util.Log.i("emuroot", report);        // visible in the CI log either way
        assertTrue("an app must be able to use emuroot's full engine:\n" + report, Emuroot.isFullyAvailable());
    }

    @Test
    public void reportsAVersion() {
        assertTrue(Emuroot.version().startsWith("emuroot "));
    }

    // --- the guest ----------------------------------------------------------------------------

    @Test
    public void runsACommandAndReturnsItsOutputAndStatus() throws Exception {
        Result r = sh("echo to-stdout; echo to-stderr >&2; exit 5");
        assertEquals("to-stdout\n", r.out);
        assertEquals("to-stderr\n", r.err);
        assertEquals(5, r.status);
    }

    @Test
    public void theGuestIsRoot() throws Exception {
        Result r = sh("id");
        assertTrue(r.out, r.out.startsWith("uid=0"));
    }

    @Test
    public void writesLandInsideTheFakeRoot() throws Exception {
        assertEquals(0, sh("echo written > /tmp/out").status);
        assertEquals("written\n", slurp(new File(root, "tmp/out")));
    }

    @Test
    public void readsComeFromTheFakeRoot() throws Exception {
        write(new File(root, "etc/hostname"), "fake-host\n");
        assertEquals("fake-host\n", sh("cat /etc/hostname").out);
    }

    @Test
    public void theRealFilesystemIsNotVisible() throws Exception {
        File marker = new File(context.getCacheDir(), "marker");
        write(marker, "real\n");
        // the same absolute path, but inside the root: it is not there
        Result r = sh("cat " + marker.getAbsolutePath());
        assertFalse(r.out, r.out.contains("real"));
    }

    @Test
    public void absoluteSymlinksStayInsideTheRoot() throws Exception {
        write(new File(root, "etc/hostname"), "fake-host\n");
        // some toolboxes (Android 5) have an `ln` that cannot make symlinks; that is not emuroot's doing
        Result ln = sh("ln -s /etc/hostname /tmp/link");
        org.junit.Assume.assumeTrue("this device's ln cannot make symlinks: " + ln.err, ln.status == 0);
        assertEquals("fake-host\n", sh("cat /tmp/link").out);
    }

    @Test
    public void dotDotCannotEscapeTheRoot() throws Exception {
        write(new File(root, "etc/hostname"), "fake-host\n");
        assertEquals("fake-host\n", sh("cd /tmp; cat ../../../../../etc/hostname").out);
    }

    @Test
    public void standardInputReachesTheGuest() throws Exception {
        EmurootProcess p = emuroot.start(Arrays.asList("/system/bin/cat"), env);
        try (OutputStream in = p.getOutputStream()) {
            in.write("through the pipe\n".getBytes(StandardCharsets.UTF_8));
        }
        assertEquals("through the pipe\n", slurp(p.getInputStream()));
        assertEquals(0, p.waitFor());
    }

    @Test
    public void manyGuestsAtOnce() throws Exception {
        List<EmurootProcess> running = new ArrayList<>();
        for (int i = 0; i < 8; i++) {
            running.add(emuroot.start(Arrays.asList("/system/bin/sh", "-c", "echo guest-" + i + "; exit " + i), env));
        }
        for (int i = 0; i < 8; i++) {
            assertEquals("guest-" + i + "\n", slurp(running.get(i).getInputStream()));
            assertEquals(i, running.get(i).waitFor());
        }
    }

    @Test
    public void destroyStopsTheGuestAndItsChildren() throws Exception {
        EmurootProcess p = emuroot.start(Arrays.asList("/system/bin/sh", "-c", "while :; do :; done"), env);
        Thread.sleep(500);
        p.destroy();
        assertEquals(128 + 15, p.waitFor());          // SIGTERM
    }

    @Test
    public void theAppsOtherChildProcessesAreLeftAlone() throws Exception {
        Process mine = new ProcessBuilder("/system/bin/sh", "-c", "sleep 1; exit 42").start();
        assertEquals(0, sh("echo hi").status);
        assertEquals(42, mine.waitFor());             // still reapable by the app
    }

    @Test
    public void fakeUnameAndIdentityAreConfigurable() throws Exception {
        try (Emuroot e = new Emuroot.Builder().root(root)
                .bind("/system", "/system").bind("/apex", "/apex").bind("/linkerconfig", "/linkerconfig")
                .bind("/dev", "/dev").bind("/proc", "/proc")
                .fakeIds(2000, 2000).build()) {
            EmurootProcess p = e.start(Arrays.asList("/system/bin/id"), env);
            String out = slurp(p.getInputStream());
            assertTrue(out, out.startsWith("uid=2000"));
        }
    }

    // --- helpers ------------------------------------------------------------------------------

    private static final class Result {
        String out, err;
        int status;
    }

    private Result sh(String script) throws IOException {
        EmurootProcess p = emuroot.start(Arrays.asList("/system/bin/sh", "-c", script), env);
        Result r = new Result();
        r.out = slurp(p.getInputStream());
        r.err = slurp(p.getErrorStream());
        r.status = p.waitFor();
        android.util.Log.i("emuroot", "sh -c '" + script + "' -> status " + r.status + ", out [" + r.out + "], err [" + r.err + "]");
        return r;
    }

    private static String slurp(InputStream in) throws IOException {
        ByteArrayOutputStream buf = new ByteArrayOutputStream();
        byte[] chunk = new byte[4096];
        for (int n; (n = in.read(chunk)) > 0; ) buf.write(chunk, 0, n);
        return buf.toString("UTF-8");
    }

    private static String slurp(File f) throws IOException {
        try (InputStream in = new java.io.FileInputStream(f)) {
            return slurp(in);
        }
    }

    private static void write(File f, String text) throws IOException {
        f.getParentFile().mkdirs();
        try (FileOutputStream out = new FileOutputStream(f)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        }
    }

    private static void deleteTree(File f) {
        File[] kids = f.listFiles();
        if (kids != null) for (File k : kids) deleteTree(k);
        f.delete();
    }
}
