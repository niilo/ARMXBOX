package org.armxbox.fexprobe;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

public final class MainActivity extends Activity {
    private static native String runProbe();
    private static String report = "No fixtures executed.";
    private static boolean started;
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(24, 48, 24, 24);
        TextView title = new TextView(this);
        title.setText("ARMXBOX FEX Probe • phase0.5\nCPU diagnostics only • no game support\n");
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
            report = "Running CPU fixtures…\nIf the process exits or hangs, capture logcat tagged ARMXBOX-FEX.\nForce-stop and relaunch to repeat.";
            output.setText(report);
            new Thread(() -> {
                try {
                    System.loadLibrary("armxbox_fex_diagnostic");
                    report = "Device: " + android.os.Build.MANUFACTURER + " " + android.os.Build.MODEL
                        + "\nAndroid API: " + android.os.Build.VERSION.SDK_INT + "\n" + runProbe();
                } catch (Throwable error) {
                    report = "FAIL native library: " + error;
                }
                runOnUiThread(() -> output.setText(report));
            }, "ARMXBOX-FEX-probe").start();
        });
    }
}
