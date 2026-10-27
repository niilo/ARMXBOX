package org.armxbox.fexprobe;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.lang.ref.WeakReference;
import java.nio.charset.StandardCharsets;

public final class MainActivity extends Activity {
    private static native String runProbe(String tracePath);
    private static native boolean pcmReady();
    private static native long beginPcmTest();
    private static native void cancelPcmTest();
    private static native void abandonPcmTest(long token);
    private static native String runPcmTest(long token, String tracePath);
    private static final String EMPTY = "No fixtures executed.";
    private static final Handler MAIN = new Handler(Looper.getMainLooper());
    private static volatile String report = EMPTY;
    private static volatile String cpuReport = EMPTY;
    private static volatile boolean started, cpuRunning, cpuPassed, libraryLoaded, audioRunning;
    // Main-thread ownership prevents a destroyed old Activity cancelling a newer run.
    private static long nextOwner, activeAudioOwner, activeAudioToken;
    private static WeakReference<MainActivity> foreground = new WeakReference<>(null);
    private final long owner = ++nextOwner;
    private boolean resumed;
    private TextView output;
    private Button run, play, stop;
    private static File traceFile(File directory) { return new File(directory, "probe-progress.txt"); }
    private static File audioTraceFile(File directory) { return new File(directory, "pcm-progress.txt"); }
    private static File resultFile(File directory) { return new File(directory, "probe-result.txt"); }
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
    private static String device() {
        return "Device: " + android.os.Build.MANUFACTURER + " " + android.os.Build.MODEL
            + "\nAndroid API: " + android.os.Build.VERSION.SDK_INT + "\n";
    }
    private void refresh() {
        if (!resumed || isFinishing() || isDestroyed() || output == null) return;
        output.setText(report);
        run.setEnabled(!started && !audioRunning);
        play.setEnabled(cpuPassed && libraryLoaded && !cpuRunning && !audioRunning && pcmReady());
        stop.setEnabled(audioRunning && activeAudioOwner == owner);
    }
    private void cancelOwnedAudio() {
        if (libraryLoaded && audioRunning && activeAudioOwner == owner)
            cancelPcmTest();
    }
    private static void refreshLater() {
        MAIN.post(() -> {
            MainActivity activity = foreground.get();
            if (activity != null) activity.refresh();
        });
    }
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        final File directory = getFilesDir();
        if (!started && EMPTY.equals(report)) {
            try {
                String trace = read(traceFile(directory));
                if (!trace.isEmpty() && !trace.endsWith("COMPLETE\n")) {
                    report = "Previous diagnostic did not finish. Cause is not yet known.\n"
                        + "Share this recovered progress report before starting another run.\n\n" + trace;
                } else {
                    String saved = read(resultFile(directory));
                    if (!saved.isEmpty()) report = saved;
                    String audioTrace = read(audioTraceFile(directory));
                    if (!audioTrace.isEmpty() && !audioTrace.endsWith("COMPLETE\n"))
                        report += "\n\nPrevious PCM test did not finish. Share this recovered progress before rerunning.\n" + audioTrace;
                }
            } catch (Exception error) {
                report = "FAIL reading saved diagnostic: " + error;
            }
        }
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(24, 48, 24, 24);
        TextView title = new TextView(this);
        title.setText("ARMXBOX FEX Probe • phase0.12c\nCPU + offscreen Vulkan; separate 0.5s PCM test\n");
        layout.addView(title);
        output = new TextView(this);
        output.setTextIsSelectable(true);
        output.setText(report);
        run = new Button(this);
        run.setText("Run CPU + GPU fixtures");
        run.setEnabled(!started);
        layout.addView(run);
        play = new Button(this);
        play.setText("Play 0.5s PCM test");
        play.setEnabled(false);
        layout.addView(play);
        stop = new Button(this);
        stop.setText("Stop PCM");
        stop.setEnabled(false);
        stop.setOnClickListener(view -> { cancelOwnedAudio(); stop.setEnabled(false); });
        layout.addView(stop);
        Button share = new Button(this);
        share.setText("Share report");
        share.setOnClickListener(view -> {
            final boolean cpuPending = cpuRunning, audioPending = audioRunning;
            String shared = report;
            // A live hung worker does not trigger process-restart recovery.
            // Include its bounded journal directly when sharing in that case.
            try {
                if (cpuPending) shared += "\n\nCurrent CPU/GPU progress:\n" + read(traceFile(directory));
                if (audioPending) shared += "\n\nCurrent PCM progress:\n" + read(audioTraceFile(directory));
            } catch (Exception error) { shared += "\nWARNING reading current progress: " + error; }
            startActivity(Intent.createChooser(new Intent(Intent.ACTION_SEND).setType("text/plain")
                .putExtra(Intent.EXTRA_TEXT, shared), "Share diagnostic report"));
        });
        layout.addView(share);
        ScrollView scroll = new ScrollView(this);
        scroll.addView(output);
        layout.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
        setContentView(layout);
        run.setOnClickListener(view -> {
            if (!resumed || started || audioRunning) return;
            started = true;
            cpuRunning = true;
            cpuPassed = false;
            report = "Running CPU + GPU fixtures…\nProgress is saved on this device.\n"
                + "If the app closes, reopen it and Share report before another run.";
            refresh();
            try {
                new Thread(() -> {
                    try {
                        write(traceFile(directory), device() + "ARMXBOX FEX phase0.12c/code18\nJava: loading native library\n", false);
                        System.loadLibrary("armxbox_fex_diagnostic");
                        libraryLoaded = true;
                        write(traceFile(directory), "Java: library loaded; entering JNI\n", true);
                        cpuReport = device() + runProbe(traceFile(directory).getAbsolutePath());
                        cpuPassed = cpuReport.contains("Result: CPU AND GPU FIXTURES PASS;");
                        report = cpuReport;
                        write(resultFile(directory), report, false);
                        write(traceFile(directory), "COMPLETE\n", true);
                    } catch (Throwable error) {
                        report = "FAIL diagnostic: " + error;
                        try {
                            report += "\n\n" + read(traceFile(directory));
                            write(resultFile(directory), report, false);
                            write(traceFile(directory), "COMPLETE\n", true);
                        } catch (Exception ignored) { }
                    } finally {
                        cpuRunning = false;
                        refreshLater();
                    }
                }, "ARMXBOX-FEX-probe").start();
            } catch (Throwable error) {
                started = false;
                cpuRunning = false;
                report = "FAIL starting CPU/GPU worker: " + error;
                refresh();
            }
        });
        play.setOnClickListener(view -> {
            if (!resumed || !cpuPassed || !libraryLoaded || cpuRunning || audioRunning) return;
            // Reserve before posting work. Lifecycle cancellation after this point
            // remains latched even if the worker has not entered native code yet.
            final long token = beginPcmTest();
            if (token == 0) { refresh(); return; }
            try {
                activeAudioOwner = owner;
                activeAudioToken = token;
                audioRunning = true;
                report = cpuReport + "\n\nPCM test running… Stop or leaving this screen cancels playback.\n";
                refresh();
                Thread worker = new Thread(() -> {
                    try {
                        write(audioTraceFile(directory), device() + "ARMXBOX FEX phase0.12c/code18\nJava: reserved PCM test; entering JNI\n", false);
                        String audio = runPcmTest(token, audioTraceFile(directory).getAbsolutePath());
                        report = cpuReport + "\n\n" + audio;
                        write(resultFile(directory), report, false);
                        write(audioTraceFile(directory), "COMPLETE\n", true);
                    } catch (Throwable error) {
                        // The native entry always catches C++ exceptions; this also
                        // covers Java journal failures before entering that entry.
                        cancelPcmTest();
                        abandonPcmTest(token);
                        report = cpuReport + "\n\nFAIL PCM test: " + error;
                        try {
                            report += "\n" + read(audioTraceFile(directory));
                            write(resultFile(directory), report, false);
                            write(audioTraceFile(directory), "COMPLETE\n", true);
                        } catch (Exception ignored) { }
                    } finally {
                        audioRunning = false;
                        MAIN.post(() -> {
                            if (activeAudioToken == token) {
                                activeAudioOwner = 0;
                                activeAudioToken = 0;
                            }
                            MainActivity activity = foreground.get();
                            if (activity != null) activity.refresh();
                        });
                    }
                }, "ARMXBOX-PCM-controller");
                worker.start();
            } catch (Throwable error) {
                cancelPcmTest();
                abandonPcmTest(token);
                audioRunning = false;
                activeAudioOwner = 0;
                activeAudioToken = 0;
                report = cpuReport + "\n\nFAIL starting PCM controller: " + error;
                refresh();
            }
        });
    }
    @Override protected void onResume() {
        super.onResume();
        foreground = new WeakReference<>(this);
        resumed = true;
        refresh();
    }
    @Override protected void onPause() {
        resumed = false;
        if (foreground.get() == this) foreground = new WeakReference<>(null);
        cancelOwnedAudio();
        super.onPause();
    }
    @Override protected void onStop() { cancelOwnedAudio(); super.onStop(); }
    @Override protected void onDestroy() { cancelOwnedAudio(); output = null; super.onDestroy(); }
}
