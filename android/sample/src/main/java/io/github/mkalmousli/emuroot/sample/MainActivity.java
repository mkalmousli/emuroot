package io.github.mkalmousli.emuroot.sample;

import android.app.Activity;
import android.os.Bundle;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.InputStream;
import java.util.HashMap;
import java.util.Map;

import io.github.mkalmousli.emuroot.Emuroot;
import io.github.mkalmousli.emuroot.EmurootProcess;

/** Runs a few commands in a fake root made inside the app's private storage. */
public class MainActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        TextView view = new TextView(this);
        view.setTextIsSelectable(true);
        view.setTypeface(android.graphics.Typeface.MONOSPACE);
        ScrollView scroll = new ScrollView(this);
        scroll.addView(view);
        setContentView(scroll);

        new Thread(() -> {
            String text;
            try {
                text = run();
            } catch (Exception e) {
                text = "failed: " + e;
            }
            final String shown = text;
            runOnUiThread(() -> view.setText(shown));
        }).start();
    }

    private String run() throws Exception {
        StringBuilder out = new StringBuilder(Emuroot.version()).append('\n').append(Emuroot.diagnose()).append('\n');

        File root = new File(getFilesDir(), "rootfs");
        new File(root, "tmp").mkdirs();
        new File(root, "etc").mkdirs();
        try (Emuroot emuroot = new Emuroot.Builder()
                .root(root)
                .bind("/system", "/system").bind("/apex", "/apex").bind("/linkerconfig", "/linkerconfig")
                .bind("/dev", "/dev").bind("/proc", "/proc")
                .fakeIds(0, 0)
                .build()) {
            Map<String, String> env = new HashMap<>();
            env.put("PATH", "/system/bin");
            EmurootProcess p = emuroot.start(java.util.Arrays.asList("/system/bin/sh", "-c",
                    "id; echo hello > /tmp/hello; cat /tmp/hello; ls /"), env);
            out.append(read(p.getInputStream())).append(read(p.getErrorStream()));
            out.append("exit status ").append(p.waitFor()).append('\n');
        }
        return out.toString();
    }

    private static String read(InputStream in) throws Exception {
        ByteArrayOutputStream buf = new ByteArrayOutputStream();
        byte[] chunk = new byte[4096];
        for (int n; (n = in.read(chunk)) > 0; ) buf.write(chunk, 0, n);
        return buf.toString("UTF-8");
    }
}
