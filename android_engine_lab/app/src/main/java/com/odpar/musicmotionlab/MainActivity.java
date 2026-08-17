package com.odpar.musicmotionlab;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.MediaStore;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.ArrayAdapter;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

import java.io.OutputStream;
import java.io.InputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicInteger;

public class MainActivity extends Activity {
    static { System.loadLibrary("odpar_lab"); }

    private native String nativeEngineInfo();
    private native String nativeSelfTest();
    private native String nativeSpineReport();
    private native int[] nativeRenderScene(int width, int height, double yaw, double pitch,
                                           double distance, double phase, boolean shadows);
    private native String nativeLastError();
    private native String nativeStressTest(int frames, int size, boolean shadows);
    private native String nativeAnalyzeAudioFile(String path);

    private static final int REQUEST_AUDIO = 4107;

    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private final Handler ui = new Handler(Looper.getMainLooper());
    private final AtomicInteger renderGeneration = new AtomicInteger();
    private ImageView viewport;
    private TextView viewportStats;
    private TextView console;
    private TextView engineBadge;
    private SeekBar yawBar, pitchBar, distanceBar, phaseBar;
    private Switch shadowSwitch;
    private Spinner resolutionSpinner;
    private Bitmap lastBitmap;
    private boolean animating = false;
    private double animationPhase = 0.0;
    private final Runnable animationTick = new Runnable() {
        @Override public void run() {
            if (!animating) return;
            animationPhase += 0.018;
            if (animationPhase >= 1.0) animationPhase -= 1.0;
            phaseBar.setProgress((int)Math.round(animationPhase * 1000.0));
            requestRender(false);
            ui.postDelayed(this, 50);
        }
    };

    @Override protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().setStatusBarColor(Color.rgb(7,9,13));
        getWindow().setNavigationBarColor(Color.rgb(7,9,13));
        setContentView(buildUi());
        worker.submit(() -> {
            final String info = nativeEngineInfo();
            ui.post(() -> {
                engineBadge.setText(extractEngineBadge(info));
                console.setText(info);
                requestRender(true);
            });
        });
    }

    @Override protected void onDestroy() {
        animating = false;
        ui.removeCallbacks(animationTick);
        worker.shutdownNow();
        super.onDestroy();
    }

    private View buildUi() {
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(Color.rgb(7,9,13));
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(16), dp(18), dp(16), dp(28));
        scroll.addView(root, new ScrollView.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        TextView title = text("ODPAR: MUSIC MOTION", 24, Color.WHITE, true);
        root.addView(title);
        TextView subtitle = text("ENGINE LAB · ANDROID / ARM64", 12, Color.rgb(132,151,184), true);
        root.addView(subtitle, lp(-1, dp(30)));
        engineBadge = text("Cargando motor oficial…", 13, Color.rgb(116,230,175), true);
        engineBadge.setBackgroundColor(Color.rgb(16,29,27));
        engineBadge.setPadding(dp(12), dp(10), dp(12), dp(10));
        root.addView(engineBadge, lp(-1, -2));
        addSpace(root, 14);

        addSection(root, "3D LAB — RASTER ODPAR");
        viewport = new ImageView(this);
        viewport.setBackgroundColor(Color.BLACK);
        viewport.setScaleType(ImageView.ScaleType.FIT_CENTER);
        root.addView(viewport, lp(-1, dp(330)));
        viewportStats = text("Esperando primer frame…", 12, Color.rgb(151,163,184), false);
        root.addView(viewportStats, lp(-1, dp(34)));

        yawBar = addSlider(root, "Órbita Yaw", 0, 360, 28);
        pitchBar = addSlider(root, "Pitch", 0, 120, 48);
        distanceBar = addSlider(root, "Distancia", 0, 100, 42);
        phaseBar = addSlider(root, "Fase / movimiento", 0, 1000, 170);

        LinearLayout row1 = row();
        shadowSwitch = new Switch(this);
        shadowSwitch.setText("Sombras ODPAR");
        shadowSwitch.setTextColor(Color.WHITE);
        shadowSwitch.setChecked(true);
        row1.addView(shadowSwitch, new LinearLayout.LayoutParams(0, -2, 1f));
        resolutionSpinner = new Spinner(this);
        String[] sizes = {"256² rápido", "384²", "512² detalle"};
        ArrayAdapter<String> adapter = new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, sizes);
        resolutionSpinner.setAdapter(adapter);
        resolutionSpinner.setSelection(1);
        row1.addView(resolutionSpinner, new LinearLayout.LayoutParams(0, dp(52), 1f));
        root.addView(row1);

        LinearLayout row2 = row();
        Button render = button("RENDER");
        render.setOnClickListener(v -> requestRender(true));
        row2.addView(render, new LinearLayout.LayoutParams(0, dp(52), 1f));
        Button animate = button("ANIMAR");
        animate.setOnClickListener(v -> {
            animating = !animating;
            animate.setText(animating ? "DETENER" : "ANIMAR");
            if (animating) ui.post(animationTick); else ui.removeCallbacks(animationTick);
        });
        row2.addView(animate, new LinearLayout.LayoutParams(0, dp(52), 1f));
        Button reset = button("RESET");
        reset.setOnClickListener(v -> { yawBar.setProgress(28); pitchBar.setProgress(48); distanceBar.setProgress(42); phaseBar.setProgress(170); requestRender(true); });
        row2.addView(reset, new LinearLayout.LayoutParams(0, dp(52), 1f));
        root.addView(row2);

        Button saveFrame = button("GUARDAR FRAME PNG");
        saveFrame.setOnClickListener(v -> saveCurrentFrame());
        root.addView(saveFrame, lp(-1, dp(50)));
        addSpace(root, 18);

        addSection(root, "MUSIC SPINE — AUDIO REAL");
        Button loadAudio = button("CARGAR AUDIO · ANALIZAR CON ODPAR");
        loadAudio.setOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("audio/*");
            startActivityForResult(intent, REQUEST_AUDIO);
        });
        root.addView(loadAudio, lp(-1, dp(54)));
        TextView musicHint = text("WAV se decodifica nativamente; la app muestra la cadena canonical 48 kHz → análisis → Music Reaction.", 11, Color.rgb(151,163,184), false);
        root.addView(musicHint, lp(-1, dp(48)));

        addSection(root, "ENGINE VERIFICATION");
        LinearLayout verifyRow = row();
        Button selftest = button("SELFTEST C");
        selftest.setOnClickListener(v -> runTask("Ejecutando selftest nativo…", this::nativeSelfTest));
        verifyRow.addView(selftest, new LinearLayout.LayoutParams(0, dp(54), 1f));
        Button stress = button("STRESS 30F");
        stress.setOnClickListener(v -> {
            int size = selectedSize(); boolean sh = shadowSwitch.isChecked();
            runTask("Renderizando 30 frames…", () -> nativeStressTest(30, size, sh));
        });
        verifyRow.addView(stress, new LinearLayout.LayoutParams(0, dp(54), 1f));
        root.addView(verifyRow);

        Button spine = button("SPINE COMPLETO · 184 MÓDULOS / CAPACIDADES");
        spine.setOnClickListener(v -> runTask("Leyendo Spine del motor…", this::nativeSpineReport));
        root.addView(spine, lp(-1, dp(54)));
        Button info = button("IDENTIDAD / ABI / SOURCE HASH");
        info.setOnClickListener(v -> runTask("Leyendo identidad…", this::nativeEngineInfo));
        root.addView(info, lp(-1, dp(54)));

        addSection(root, "CONSOLE / EVIDENCE");
        console = text("Inicializando…", 11, Color.rgb(190,199,215), false);
        console.setTextIsSelectable(true);
        console.setTypeface(android.graphics.Typeface.MONOSPACE);
        console.setPadding(dp(12), dp(12), dp(12), dp(12));
        console.setBackgroundColor(Color.rgb(12,16,23));
        root.addView(console, lp(-1, dp(300)));
        Button copy = button("COPIAR EVIDENCIA");
        copy.setOnClickListener(v -> {
            ClipboardManager cb = (ClipboardManager)getSystemService(Context.CLIPBOARD_SERVICE);
            cb.setPrimaryClip(ClipData.newPlainText("ODPAR evidence", console.getText()));
            Toast.makeText(this, "Copiado", Toast.LENGTH_SHORT).show();
        });
        root.addView(copy, lp(-1, dp(48)));
        return scroll;
    }

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_AUDIO || resultCode != RESULT_OK || data == null || data.getData() == null) return;
        final Uri uri = data.getData();
        console.setText("Importando audio al sandbox privado de ODPAR…");
        worker.submit(() -> {
            File temp = null;
            String result;
            try {
                temp = copyUriToCache(uri);
                result = nativeAnalyzeAudioFile(temp.getAbsolutePath());
            } catch (Throwable t) {
                result = "ERROR importando/analizando audio: " + t;
            } finally {
                if (temp != null && temp.exists()) temp.delete();
            }
            final String finalResult = result;
            ui.post(() -> console.setText(finalResult));
        });
    }

    private File copyUriToCache(Uri uri) throws Exception {
        File outFile = File.createTempFile("odpar_music_", ".media", getCacheDir());
        try (InputStream in = getContentResolver().openInputStream(uri);
             FileOutputStream out = new FileOutputStream(outFile)) {
            if (in == null) throw new IllegalStateException("ContentResolver returned null stream");
            byte[] buffer = new byte[1024 * 1024];
            int n;
            while ((n = in.read(buffer)) >= 0) {
                if (n > 0) out.write(buffer, 0, n);
            }
            out.getFD().sync();
        }
        return outFile;
    }

    private void requestRender(boolean announce) {
        final int generation = renderGeneration.incrementAndGet();
        final int size = selectedSize();
        final double yaw = yawBar.getProgress();
        final double pitch = pitchBar.getProgress() - 60.0;
        final double distance = 2.1 + distanceBar.getProgress() * 0.075;
        final double phase = phaseBar.getProgress() / 1000.0;
        final boolean shadows = shadowSwitch.isChecked();
        if (announce) viewportStats.setText("Renderizando con ODPAR…");
        worker.submit(() -> {
            long t0 = System.nanoTime();
            int[] pixels = nativeRenderScene(size, size, yaw, pitch, distance, phase, shadows);
            long t1 = System.nanoTime();
            String nativeStatus = nativeLastError();
            if (generation != renderGeneration.get()) return;
            ui.post(() -> {
                if (pixels == null) {
                    viewportStats.setText("ERROR · " + nativeStatus);
                    return;
                }
                Bitmap bmp = Bitmap.createBitmap(pixels, size, size, Bitmap.Config.ARGB_8888);
                lastBitmap = bmp;
                viewport.setImageBitmap(bmp);
                double ms = (t1 - t0) / 1_000_000.0;
                viewportStats.setText(String.format(Locale.US, "%d×%d · %.1f ms · %s · %s",
                        size, size, ms, shadows ? "shadows" : "no shadows", nativeStatus));
            });
        });
    }

    private void runTask(String status, NativeTextTask task) {
        console.setText(status);
        worker.submit(() -> {
            String result;
            try { result = task.run(); }
            catch (Throwable t) { result = "ERROR: " + t; }
            final String finalResult = result;
            ui.post(() -> console.setText(finalResult));
        });
    }

    private interface NativeTextTask { String run(); }

    private SeekBar addSlider(LinearLayout root, String label, int min, int max, int progress) {
        TextView t = text(label, 12, Color.rgb(187,197,216), true);
        root.addView(t, lp(-1, dp(24)));
        SeekBar b = new SeekBar(this);
        if (Build.VERSION.SDK_INT >= 26) b.setMin(min);
        b.setMax(max);
        b.setProgress(progress);
        b.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            public void onProgressChanged(SeekBar seekBar, int p, boolean fromUser) { if(fromUser) requestRender(false); }
            public void onStartTrackingTouch(SeekBar seekBar) {}
            public void onStopTrackingTouch(SeekBar seekBar) { requestRender(true); }
        });
        root.addView(b, lp(-1, dp(40)));
        return b;
    }

    private int selectedSize() {
        int p = resolutionSpinner == null ? 1 : resolutionSpinner.getSelectedItemPosition();
        return p == 0 ? 256 : (p == 2 ? 512 : 384);
    }

    private String extractEngineBadge(String info) {
        if (info == null) return "MOTOR NO DISPONIBLE";
        int i = info.indexOf("version=");
        int j = i >= 0 ? info.indexOf('\n', i) : -1;
        String v = i >= 0 ? info.substring(i + 8, j > i ? j : info.length()) : "unknown";
        return "● MOTOR C CARGADO · " + v + " · ARM64";
    }

    private void saveCurrentFrame() {
        Bitmap bmp = lastBitmap;
        if (bmp == null) { Toast.makeText(this, "Renderiza un frame primero", Toast.LENGTH_SHORT).show(); return; }
        worker.submit(() -> {
            try {
                String name = "ODPAR_MusicMotion_" + System.currentTimeMillis() + ".png";
                ContentValues values = new ContentValues();
                values.put(MediaStore.Images.Media.DISPLAY_NAME, name);
                values.put(MediaStore.Images.Media.MIME_TYPE, "image/png");
                if (Build.VERSION.SDK_INT >= 29) values.put(MediaStore.Images.Media.RELATIVE_PATH, Environment.DIRECTORY_PICTURES + "/ODPAR");
                Uri uri = getContentResolver().insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values);
                if (uri == null) throw new IllegalStateException("MediaStore insert failed");
                try (OutputStream os = getContentResolver().openOutputStream(uri)) {
                    if (os == null || !bmp.compress(Bitmap.CompressFormat.PNG, 100, os)) throw new IllegalStateException("PNG encode failed");
                }
                ui.post(() -> Toast.makeText(this, "Frame guardado en Pictures/ODPAR", Toast.LENGTH_LONG).show());
            } catch (Throwable t) {
                ui.post(() -> Toast.makeText(this, "No se pudo guardar: " + t.getMessage(), Toast.LENGTH_LONG).show());
            }
        });
    }

    private void addSection(LinearLayout root, String s) {
        TextView t = text(s, 12, Color.rgb(108,172,255), true);
        t.setPadding(0, dp(10), 0, dp(8));
        root.addView(t, lp(-1, dp(42)));
    }

    private TextView text(String s, int sp, int color, boolean bold) {
        TextView t = new TextView(this); t.setText(s); t.setTextSize(sp); t.setTextColor(color);
        if (bold) t.setTypeface(android.graphics.Typeface.DEFAULT, android.graphics.Typeface.BOLD);
        t.setGravity(Gravity.CENTER_VERTICAL); return t;
    }

    private Button button(String s) {
        Button b = new Button(this); b.setText(s); b.setTextColor(Color.WHITE); b.setTextSize(11);
        b.setBackgroundColor(Color.rgb(24,33,48)); b.setAllCaps(false); b.setPadding(dp(6),0,dp(6),0); return b;
    }

    private LinearLayout row() {
        LinearLayout r = new LinearLayout(this); r.setOrientation(LinearLayout.HORIZONTAL); r.setGravity(Gravity.CENTER_VERTICAL); return r;
    }
    private void addSpace(LinearLayout root, int dp) { View v=new View(this); root.addView(v,lp(-1,dp(dp))); }
    private LinearLayout.LayoutParams lp(int w, int h) { return new LinearLayout.LayoutParams(w,h); }
    private int dp(int v) { return (int)(v * getResources().getDisplayMetrics().density + 0.5f); }
}
