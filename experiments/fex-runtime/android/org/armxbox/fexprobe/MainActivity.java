package org.armxbox.fexprobe;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;

public final class MainActivity extends Activity {
    private static native String runProbe(String tracePath);
    private static final String EMPTY = "No fixtures executed.";
    private static volatile String report = EMPTY;
    private static boolean started;
    private File traceFile() { return new File(getFilesDir(), "probe-progress.txt"); }
    private File resultFile() { return new File(getFilesDir(), "probe-result.txt"); }
    private static String read(File file) throws Exception {
        if (!file.exists()) return "";
        try (FileInputStream input = new FileInputStream(file);
             ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[4096];
            int count;
            while (output.size() < 132 * 1024 && (count = input.read(buffer)) > 0)
                output.write(buffer, 0, Math.min(count, 132 * 1024 - output.size()));
            return new String(output.toByteArray(), StandardCharsets.UTF_8);
        }
    }
    private static void write(File file, String text, boolean append) throws Exception {
        try (FileOutputStream output = new FileOutputStream(file, append)) {
            output.write(text.getBytes(StandardCharsets.UTF_8));
            output.getFD().sync();
        }
    }
    private String device() {
        return "Device: " + android.os.Build.MANUFACTURER + " " + android.os.Build.MODEL
            + "\nAndroid API: " + android.os.Build.VERSION.SDK_INT + "\n";
    }
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        if (!started && EMPTY.equals(report)) {
            try {
                String trace = read(traceFile());
                if (!trace.isEmpty() && !trace.endsWith("COMPLETE\n")) {
                    report = "Previous diagnostic did not finish. Cause is not yet known.\n"
                        + "Share this recovered progress report before starting another run.\n\n" + trace;
                } else {
                    String saved = read(resultFile());
                    if (!saved.isEmpty()) report = saved;
                }
            } catch (Exception error) {
                report = "FAIL reading saved diagnostic: " + error;
            }
        }
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(24, 48, 24, 24);
        TextView title = new TextView(this);
        title.setText("ARMXBOX FEX Probe • phase0.11.1\nCPU diagnostics only • no game support\n");
        layout.addView(title);
        TextView output = new TextView(this);
        output.setTextIsSelectable(true);
        output.setText(report);
        Button run = new Button(this);
        run.setText("Run CPU fixtures");
        run.setEnabled(!started);
        layout.addView(run);
        Button share = new Button(this);
        share.setText("Share report");
        share.setOnClickListener(view -> startActivity(Intent.createChooser(
            new Intent(Intent.ACTION_SEND).setType("text/plain").putExtra(Intent.EXTRA_TEXT, report), "Share diagnostic report")));
        layout.addView(share);
        ScrollView scroll = new ScrollView(this);
        scroll.addView(output);
        layout.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
        setContentView(layout);
        run.setOnClickListener(view -> {
            started = true;
            run.setEnabled(false);
            report = "Running CPU fixtures…\nProgress is saved on this device.\n"
                + "If the app closes, reopen it and Share report before another run.";
            output.setText(report);
            new Thread(() -> {
                try {
                    write(traceFile(), device() + "ARMXBOX FEX phase0.11.1/code15\nJava: loading native library\n", false);
                    System.loadLibrary("armxbox_fex_diagnostic");
                    write(traceFile(), "Java: library loaded; entering JNI\n", true);
                    report = device() + runProbe(traceFile().getAbsolutePath());
                    write(resultFile(), report, false);
                    write(traceFile(), "COMPLETE\n", true);
                } catch (Throwable error) {
                    report = "FAIL diagnostic: " + error;
                    try {
                        report += "\n\n" + read(traceFile());
                        write(resultFile(), report, false);
                        write(traceFile(), "COMPLETE\n", true);
                    } catch (Exception ignored) { }
                }
                runOnUiThread(() -> output.setText(report));
            }, "ARMXBOX-FEX-probe").start();
        });
    }
}
