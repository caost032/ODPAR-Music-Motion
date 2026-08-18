package com.odpar.musicmotion;

import android.app.Activity;
import android.content.Intent;
import android.content.res.Configuration;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.ScaleGestureDetector;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.TextView;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

/** A responsive, native APP_AUTHORING3D workstation with no support-library dependency. */
public final class MainActivity extends Activity {
    private static final int OPEN_OBJ_REQUEST = 41;
    private static final int OPEN_CORE_IMAGE_REQUEST = 42;
    private static final int BG = Color.rgb(7, 10, 17);
    private static final int SURFACE = Color.rgb(13, 18, 29);
    private static final int SURFACE_2 = Color.rgb(20, 27, 41);
    private static final int STROKE = Color.rgb(42, 55, 76);
    private static final int TEXT = Color.rgb(241, 246, 255);
    private static final int MUTED = Color.rgb(151, 167, 190);
    private static final int ACCENT = Color.rgb(94, 133, 255);
    private static final int ACCENT_DARK = Color.rgb(39, 65, 145);
    private static final int GOOD = Color.rgb(82, 213, 154);
    private static final int DANGER = Color.rgb(255, 106, 125);

    private enum Tool { COMPOSE, CAMERA, OBJECT, SURFACE, NODES }
    private interface EngineCall { int run(); }
    private interface IntSetter { void set(int value); }

    static { System.loadLibrary("odpar_authoring3d"); }

    private static native int nativeCreate(int width, int height);
    private static native void nativeDestroy();
    private static native int nativeResize(int width, int height);
    private static native int nativeImportObj(byte[] source);
    private static native int nativeFocus();
    private static native int nativeResetDemo();
    private static native int nativePan(float rightMeters, float upMeters);
    private static native int nativeDolly(float forwardMeters);
    private static native int nativeOrbit(float yawDegrees, float pitchDegrees);
    private static native int nativeMoveModel(float xMeters, float yMeters, float zMeters);
    private static native int nativeScaleModel(float factor);
    private static native int nativeRotateGeometry(int axis, float degrees);
    private static native int nativeScaleGeometry(float factor);
    private static native int nativeTranslateGeometry(float xMeters, float yMeters, float zMeters);
    private static native int nativeSetMaterial(int preset, int roughness, int metallic);
    private static native int nativeSetQuality(int quality);
    private static native int nativeAddSurfaceNode(float xPixel, float yPixel, int connect);
    private static native int nativeClearSelection();
    private static native int nativeManipulationBegin(float xPixel, float yPixel);
    private static native int nativeManipulationUpdate(float xPixel, float yPixel);
    private static native int nativeManipulationEnd();
    private static native int[] nativeNodePoints();
    private static native int[] nativeRender();
    private static native int nativeSetComposition(byte[] title, byte[] artist,
            int background, int shape, int layout, int energy);
    private static native int nativeSetCoreImage(byte[] rgba, int width, int height);
    private static native int nativeClearCoreImage();
    private static native int[] nativeRenderComposition(int width, int height,
            long sample, long duration);
    private static native String nativeSummary();

    private final ExecutorService engineExecutor = Executors.newSingleThreadExecutor(runnable -> {
        Thread thread = new Thread(runnable, "odpar-authoring3d");
        thread.setPriority(Thread.NORM_PRIORITY + 1);
        return thread;
    });
    private final AtomicBoolean frameScheduled = new AtomicBoolean();
    private final AtomicBoolean frameDirty = new AtomicBoolean();
    private volatile boolean engineReady;
    private volatile boolean destroyed;
    private volatile int requestedWidth = 512;
    private volatile int requestedHeight = 512;
    private int nativeWidth = 512;
    private int nativeHeight = 512;

    private EngineView engineView;
    private LinearLayout inspectorContent;
    private TextView statusView;
    private TextView statsView;
    private TextView viewportBadge;
    private Button cameraToolButton;
    private Button composeToolButton;
    private Button objectToolButton;
    private Button surfaceToolButton;
    private Button nodeToolButton;
    private volatile Tool activeTool = Tool.COMPOSE;
    private boolean connectNodes = true;
    private boolean addNodeArmed;
    private int qualityTier;
    private int materialPreset;
    private int roughness = 145;
    private int metallic = 35;
    private int compositionBackground = 4;
    private int compositionShape = 3;
    private int compositionLayout;
    private int compositionEnergy = 150;
    private volatile long compositionSample;
    private static final long COMPOSITION_DURATION = 48_000L * 180L;
    private String compositionTitle = "ODPAR MUSIC MOTION";
    private String compositionArtist = "AUTHORING STUDIO";
    private String statusMessage = "Iniciando motor de autoría 2D/3D…";
    private String statsMessage = "Preparando sesión nativa";

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().setStatusBarColor(BG);
        getWindow().setNavigationBarColor(BG);
        setContentView(buildUi());
        engineExecutor.execute(() -> {
            int status = nativeCreate(nativeWidth, nativeHeight);
            engineReady = status == 0;
            if (engineReady) {
                statsMessage = nativeSummary();
                postStatus("Motor listo · edición nativa activa", false);
                requestFrame();
            } else {
                postStatus("No se pudo iniciar el motor · " + statusName(status), true);
            }
        });
    }

    @Override public void onConfigurationChanged(Configuration configuration) {
        super.onConfigurationChanged(configuration);
        setContentView(buildUi());
        requestFrame();
    }

    @Override protected void onDestroy() {
        destroyed = true;
        engineExecutor.execute(() -> {
            if (engineReady) nativeDestroy();
            engineReady = false;
        });
        engineExecutor.shutdown();
        super.onDestroy();
    }

    private View buildUi() {
        boolean landscape = getResources().getConfiguration().orientation
                == Configuration.ORIENTATION_LANDSCAPE;
        LinearLayout root = vertical();
        root.setBackgroundColor(BG);
        root.addView(buildHeader(landscape), matchWrap());
        if (landscape) {
            LinearLayout workspace = horizontal();
            workspace.setPadding(dp(8), dp(4), dp(8), dp(8));
            workspace.addView(buildToolRail(), new LinearLayout.LayoutParams(dp(84), -1));
            workspace.addView(buildViewport(), widthWeight(1f));
            ScrollView inspector = buildInspectorHost();
            int availableDp = getResources().getConfiguration().screenWidthDp;
            int inspectorDp = Math.max(290, Math.min(370, availableDp / 3));
            LinearLayout.LayoutParams panel = new LinearLayout.LayoutParams(dp(inspectorDp), -1);
            panel.setMargins(dp(8), 0, 0, 0);
            workspace.addView(inspector, panel);
            root.addView(workspace, heightWeight(1f));
        } else {
            LinearLayout stage = horizontal();
            stage.setPadding(dp(6), dp(4), dp(6), dp(6));
            stage.addView(buildToolRail(), new LinearLayout.LayoutParams(dp(76), -1));
            stage.addView(buildViewport(), widthWeight(1f));
            root.addView(stage, heightWeight(1f));
            ScrollView inspector = buildInspectorHost();
            int screenHeightDp = getResources().getConfiguration().screenHeightDp;
            LinearLayout.LayoutParams panel = new LinearLayout.LayoutParams(-1,
                    dp(screenHeightDp < 650 ? 205 : 260));
            panel.setMargins(dp(6), 0, dp(6), dp(7));
            root.addView(inspector, panel);
        }
        updateToolStyles();
        buildInspector();
        return root;
    }

    private View buildHeader(boolean landscape) {
        LinearLayout header = horizontal();
        header.setGravity(Gravity.CENTER_VERTICAL);
        header.setPadding(dp(14), dp(8), dp(10), dp(7));
        header.setBackground(shape(SURFACE, 0, 0));
        LinearLayout identity = vertical();
        TextView title = text("ODPAR  /  AUTHORING STUDIO", landscape ? 18f : 15f, TEXT);
        title.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        identity.addView(title);
        statusView = text(statusMessage, landscape ? 11.5f : 10.5f, MUTED);
        statusView.setSingleLine(true);
        identity.addView(statusView);
        header.addView(identity, widthWeight(1f));
        header.addView(actionButton(activeTool == Tool.COMPOSE
                ? (landscape ? "Imagen central" : "Imagen")
                : (landscape ? "Importar OBJ" : "OBJ"),
                v -> { if (activeTool == Tool.COMPOSE) chooseCoreImage(); else chooseObj(); }));
        header.addView(actionButton(activeTool == Tool.COMPOSE
                ? (landscape ? "Core de muestra" : "Demo")
                : (landscape ? "Enfocar" : "Foco"),
                v -> {
                    if (activeTool == Tool.COMPOSE)
                        runEngine("Core de muestra restaurado", MainActivity::nativeClearCoreImage, true);
                    else runEngine("Vista enfocada", MainActivity::nativeFocus, true);
                }));
        return header;
    }

    private View buildToolRail() {
        LinearLayout rail = vertical();
        rail.setGravity(Gravity.TOP | Gravity.CENTER_HORIZONTAL);
        rail.setPadding(dp(5), dp(7), dp(5), dp(7));
        rail.setBackground(shape(SURFACE, dp(16), 1));
        TextView label = text("MODO", 9.5f, MUTED);
        label.setGravity(Gravity.CENTER);
        rail.addView(label, new LinearLayout.LayoutParams(-1, dp(30)));
        composeToolButton = toolButton("CORE 2D", v -> selectTool(Tool.COMPOSE));
        cameraToolButton = toolButton("CÁMARA", v -> selectTool(Tool.CAMERA));
        objectToolButton = toolButton("OBJETO", v -> selectTool(Tool.OBJECT));
        surfaceToolButton = toolButton("SUPERF.", v -> selectTool(Tool.SURFACE));
        nodeToolButton = toolButton("NODOS", v -> selectTool(Tool.NODES));
        rail.addView(composeToolButton);
        rail.addView(cameraToolButton);
        rail.addView(objectToolButton);
        rail.addView(surfaceToolButton);
        rail.addView(nodeToolButton);
        View spacer = new View(this);
        rail.addView(spacer, heightWeight(1f));
        TextView live = text("LIVE\nCORE", 9f, GOOD);
        live.setGravity(Gravity.CENTER);
        rail.addView(live, new LinearLayout.LayoutParams(-1, dp(40)));
        return rail;
    }

    private View buildViewport() {
        FrameLayout frame = new FrameLayout(this);
        frame.setBackground(shape(Color.rgb(10, 14, 23), dp(16), 1));
        engineView = new EngineView();
        FrameLayout.LayoutParams engineParams = new FrameLayout.LayoutParams(-1, -1);
        engineParams.setMargins(dp(1), dp(1), dp(1), dp(1));
        frame.addView(engineView, engineParams);
        viewportBadge = text(toolHint(), 10.5f, TEXT);
        viewportBadge.setPadding(dp(9), dp(5), dp(9), dp(5));
        viewportBadge.setBackground(shape(Color.argb(218, 19, 26, 40), dp(13), 1));
        FrameLayout.LayoutParams badge = new FrameLayout.LayoutParams(-2, -2,
                Gravity.LEFT | Gravity.BOTTOM);
        badge.setMargins(dp(10), dp(10), dp(10), dp(10));
        frame.addView(viewportBadge, badge);
        return frame;
    }

    private ScrollView buildInspectorHost() {
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackground(shape(SURFACE, dp(16), 1));
        inspectorContent = vertical();
        inspectorContent.setPadding(dp(14), dp(12), dp(14), dp(18));
        scroll.addView(inspectorContent, new ScrollView.LayoutParams(-1, -2));
        return scroll;
    }

    private void buildInspector() {
        if (inspectorContent == null) return;
        inspectorContent.removeAllViews();
        inspectorContent.addView(sectionHeader(toolTitle(), toolDescription()));
        if (activeTool == Tool.COMPOSE) buildCompositionInspector();
        else if (activeTool == Tool.CAMERA) buildCameraInspector();
        else if (activeTool == Tool.OBJECT) buildObjectInspector();
        else if (activeTool == Tool.SURFACE) buildSurfaceInspector();
        else buildNodeInspector();
        addDivider(inspectorContent);
        inspectorContent.addView(sectionLabel("RENDER"));
        LinearLayout qualityRow = horizontal();
        qualityRow.setGravity(Gravity.CENTER_VERTICAL);
        qualityRow.addView(text("Calidad", 12f, MUTED), widthWeight(1f));
        Spinner quality = new Spinner(this);
        quality.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item,
                new String[]{"1× Fluido", "2× Medio", "3× Alto", "4× Máximo"}));
        quality.setSelection(qualityTier, false);
        quality.setBackgroundTintList(android.content.res.ColorStateList.valueOf(ACCENT));
        quality.setOnItemSelectedListener(new android.widget.AdapterView.OnItemSelectedListener() {
            @Override public void onNothingSelected(android.widget.AdapterView<?> parent) { }
            @Override public void onItemSelected(android.widget.AdapterView<?> parent, View view,
                                                  int position, long id) {
                if (position == qualityTier) return;
                qualityTier = position;
                String message = position == 3
                        ? "Calidad 4× · supersampling máximo, consumo intensivo"
                        : "Calidad " + (position + 1) + "×";
                runEngine(message,
                        () -> nativeSetQuality(position), true);
            }
        });
        qualityRow.addView(quality, new LinearLayout.LayoutParams(dp(132), dp(44)));
        inspectorContent.addView(qualityRow);
        addDivider(inspectorContent);
        inspectorContent.addView(sectionLabel("SESIÓN NATIVA"));
        statsView = text(statsMessage, 11.5f, MUTED);
        statsView.setLineSpacing(0f, 1.18f);
        inspectorContent.addView(statsView);
    }

    private void buildCompositionInspector() {
        inspectorContent.addView(sectionLabel("CORE, COMPOSICIÓN Y METADATOS"));
        EditText title = editorField("Nombre", compositionTitle);
        EditText artist = editorField("Artista", compositionArtist);
        inspectorContent.addView(title, new LinearLayout.LayoutParams(-1, dp(45)));
        inspectorContent.addView(artist, new LinearLayout.LayoutParams(-1, dp(45)));
        LinearLayout metadataActions = horizontal();
        metadataActions.addView(wideButton("Aplicar texto", v -> {
            compositionTitle = title.getText().toString();
            compositionArtist = artist.getText().toString();
            applyComposition("Nombre y artista actualizados");
        }), widthWeight(1f));
        metadataActions.addView(wideButton("Importar imagen", v -> chooseCoreImage()),
                widthWeight(1f));
        inspectorContent.addView(metadataActions);

        inspectorContent.addView(labeledSpinner("Fondo",
                new String[]{"Ninguno", "Sólido", "Rejilla", "Perspectiva",
                        "Profundidad", "Horizonte", "Concéntrico", "Puntos",
                        "Gradiente", "Perla"}, compositionBackground,
                position -> { compositionBackground = position; applyComposition("Fondo actualizado"); }));
        inspectorContent.addView(labeledSpinner("Forma del core",
                new String[]{"Círculo", "Cuadrado", "Rectángulo redondeado"},
                compositionShape - 1,
                position -> { compositionShape = position + 1; applyComposition("Core actualizado"); }));
        inspectorContent.addView(labeledSpinner("Diseño del campo",
                new String[]{"Radial", "Lineal", "Espejo", "Onda", "Grid"},
                compositionLayout,
                position -> { compositionLayout = position; applyComposition("Composición actualizada"); }));

        LinearLayout energyGroup = vertical();
        TextView energyLabel = text("Intensidad visual  " + compositionEnergy, 11.5f, MUTED);
        energyGroup.addView(energyLabel);
        SeekBar energy = new SeekBar(this);
        energy.setMax(255);
        energy.setProgress(compositionEnergy);
        energy.setProgressTintList(android.content.res.ColorStateList.valueOf(ACCENT));
        energy.setThumbTintList(android.content.res.ColorStateList.valueOf(TEXT));
        energy.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onStartTrackingTouch(SeekBar bar) { }
            @Override public void onProgressChanged(SeekBar bar, int value, boolean fromUser) {
                compositionEnergy = value;
                energyLabel.setText("Intensidad visual  " + value);
            }
            @Override public void onStopTrackingTouch(SeekBar bar) {
                applyComposition("Intensidad visual actualizada");
            }
        });
        energyGroup.addView(energy, new LinearLayout.LayoutParams(-1, dp(38)));
        inspectorContent.addView(energyGroup);

        addDivider(inspectorContent);
        inspectorContent.addView(sectionLabel("BARRA DE TIEMPO · 3:00"));
        SeekBar timeline = new SeekBar(this);
        timeline.setMax(1800);
        timeline.setProgress((int)(compositionSample * 1800L / COMPOSITION_DURATION));
        timeline.setProgressTintList(android.content.res.ColorStateList.valueOf(GOOD));
        timeline.setThumbTintList(android.content.res.ColorStateList.valueOf(TEXT));
        timeline.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onStartTrackingTouch(SeekBar bar) { }
            @Override public void onProgressChanged(SeekBar bar, int value, boolean fromUser) {
                compositionSample = COMPOSITION_DURATION * value / 1800L;
                if (fromUser) requestFrame();
            }
            @Override public void onStopTrackingTouch(SeekBar bar) { requestFrame(); }
        });
        inspectorContent.addView(timeline, new LinearLayout.LayoutParams(-1, dp(42)));
        LinearLayout timeActions = horizontal();
        timeActions.addView(wideButton("Inicio", v -> {
            compositionSample = 0L; buildInspector(); requestFrame();
        }), widthWeight(1f));
        timeActions.addView(wideButton("Centro", v -> {
            compositionSample = COMPOSITION_DURATION / 2L; buildInspector(); requestFrame();
        }), widthWeight(1f));
        timeActions.addView(wideButton("Final", v -> {
            compositionSample = COMPOSITION_DURATION; buildInspector(); requestFrame();
        }), widthWeight(1f));
        inspectorContent.addView(timeActions);
        TextView truth = text("Render real del compositor: fondo → core → campo → HUD. "
                + "El slider controla el sample de presentación; el análisis de una canción "
                + "se conectará al importar audio mediante Music Map.", 11f, MUTED);
        truth.setPadding(0, dp(7), 0, 0);
        inspectorContent.addView(truth);
    }

    private void buildCameraInspector() {
        inspectorContent.addView(sectionLabel("ÓRBITA Y ENCUADRE"));
        LinearLayout row = horizontal();
        row.addView(wideButton("Enfocar", v ->
                runEngine("Vista enfocada", MainActivity::nativeFocus, true)), widthWeight(1f));
        row.addView(wideButton("+", v ->
                runEngine("Cámara acercada", () -> nativeDolly(0.35f), true)));
        row.addView(wideButton("−", v ->
                runEngine("Cámara alejada", () -> nativeDolly(-0.35f), true)));
        inspectorContent.addView(row);
        inspectorContent.addView(axisRow("Paneo X", "Izq.", "Der.",
                () -> nativePan(-0.25f, 0f), () -> nativePan(0.25f, 0f)));
        inspectorContent.addView(axisRow("Paneo Y", "Abajo", "Arriba",
                () -> nativePan(0f, -0.25f), () -> nativePan(0f, 0.25f)));
        inspectorContent.addView(axisRow("Órbita", "−15°", "+15°",
                () -> nativeOrbit(-15f, 0f), () -> nativeOrbit(15f, 0f)));
    }

    private void buildObjectInspector() {
        inspectorContent.addView(sectionLabel("TRANSFORMACIÓN DEL MODELO"));
        inspectorContent.addView(axisRow("Posición X", "−X", "+X",
                () -> nativeMoveModel(-0.2f, 0f, 0f), () -> nativeMoveModel(0.2f, 0f, 0f)));
        inspectorContent.addView(axisRow("Posición Y", "−Y", "+Y",
                () -> nativeMoveModel(0f, -0.2f, 0f), () -> nativeMoveModel(0f, 0.2f, 0f)));
        inspectorContent.addView(axisRow("Posición Z", "−Z", "+Z",
                () -> nativeMoveModel(0f, 0f, -0.2f), () -> nativeMoveModel(0f, 0f, 0.2f)));
        inspectorContent.addView(axisRow("Escala modelo", "90%", "110%",
                () -> nativeScaleModel(0.9f), () -> nativeScaleModel(1.1f)));
        addDivider(inspectorContent);
        inspectorContent.addView(sectionLabel("GEOMETRÍA EDITABLE"));
        inspectorContent.addView(axisRow("Rotar X", "−15°", "+15°",
                () -> nativeRotateGeometry(0, -15f), () -> nativeRotateGeometry(0, 15f)));
        inspectorContent.addView(axisRow("Rotar Y", "−15°", "+15°",
                () -> nativeRotateGeometry(1, -15f), () -> nativeRotateGeometry(1, 15f)));
        inspectorContent.addView(axisRow("Rotar Z", "−15°", "+15°",
                () -> nativeRotateGeometry(2, -15f), () -> nativeRotateGeometry(2, 15f)));
        inspectorContent.addView(axisRow("Escala malla", "90%", "110%",
                () -> nativeScaleGeometry(0.9f), () -> nativeScaleGeometry(1.1f)));
        inspectorContent.addView(axisRow("Malla X", "−X", "+X",
                () -> nativeTranslateGeometry(-0.15f, 0f, 0f),
                () -> nativeTranslateGeometry(0.15f, 0f, 0f)));
        inspectorContent.addView(axisRow("Malla Y", "−Y", "+Y",
                () -> nativeTranslateGeometry(0f, -0.15f, 0f),
                () -> nativeTranslateGeometry(0f, 0.15f, 0f)));
        inspectorContent.addView(axisRow("Malla Z", "−Z", "+Z",
                () -> nativeTranslateGeometry(0f, 0f, -0.15f),
                () -> nativeTranslateGeometry(0f, 0f, 0.15f)));
        inspectorContent.addView(wideButton("Restaurar cubo de prueba", v ->
                runEngine("Escena de prueba restaurada", MainActivity::nativeResetDemo, true)),
                new LinearLayout.LayoutParams(-1, dp(46)));
    }

    private void buildSurfaceInspector() {
        inspectorContent.addView(sectionLabel("MATERIAL PBR REAL"));
        LinearLayout colors = horizontal();
        int[] swatches = { Color.rgb(74,144,226), Color.rgb(226,82,92),
                Color.rgb(92,205,142), Color.rgb(230,178,70),
                Color.rgb(205,210,224), Color.rgb(35,39,48) };
        for (int i = 0; i < swatches.length; ++i) {
            final int preset = i;
            View swatch = new View(this);
            swatch.setContentDescription("Material " + (i + 1));
            swatch.setBackground(shape(swatches[i], dp(18), i == materialPreset ? 2 : 0));
            swatch.setOnClickListener(v -> {
                materialPreset = preset;
                applyMaterial();
                buildInspector();
            });
            LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(0, dp(36), 1f);
            p.setMargins(dp(2), dp(3), dp(2), dp(5));
            colors.addView(swatch, p);
        }
        inspectorContent.addView(colors);
        inspectorContent.addView(materialSlider("Rugosidad", roughness, value -> roughness = value));
        inspectorContent.addView(materialSlider("Metálico", metallic, value -> metallic = value));
        addDivider(inspectorContent);
        inspectorContent.addView(sectionLabel("TEXTURE EDITOR"));
        TextView note = text("La frontera Android actual expone color PBR, rugosidad y metálico. "
                + "Imagen, patches y capas quedan visibles como arquitectura, pero no se simulan "
                + "hasta que APP_AUTHORING3D publique esas operaciones.", 11.5f, MUTED);
        note.setPadding(0, 0, 0, dp(6));
        inspectorContent.addView(note);
        LinearLayout pending = horizontal();
        pending.addView(disabledButton("Imagen"), widthWeight(1f));
        pending.addView(disabledButton("Patch"), widthWeight(1f));
        pending.addView(disabledButton("Capas"), widthWeight(1f));
        inspectorContent.addView(pending);
    }

    private void buildNodeInspector() {
        inspectorContent.addView(sectionLabel("NODOS UNIVERSALES 3D"));
        TextView note = text("Crea anclajes sobre la superficie real, encadénalos y arrástralos "
                + "con manipulación directa.", 11.5f, MUTED);
        note.setPadding(0, 0, 0, dp(8));
        inspectorContent.addView(note);
        Button add = wideButton(addNodeArmed ? "Toca la superficie…" : "Añadir nodo en superficie", v -> {
            addNodeArmed = !addNodeArmed;
            postStatus(addNodeArmed ? "Toca una superficie para crear el nodo" :
                    "Inserción cancelada", false);
            buildInspector();
            updateViewportBadge();
        });
        if (addNodeArmed) add.setBackground(shape(ACCENT_DARK, dp(10), 1));
        inspectorContent.addView(add, new LinearLayout.LayoutParams(-1, dp(46)));
        inspectorContent.addView(wideButton(connectNodes ? "Ruta: conectar nodos" :
                "Ruta: nodos independientes", v -> {
            connectNodes = !connectNodes;
            buildInspector();
        }), new LinearLayout.LayoutParams(-1, dp(46)));
        inspectorContent.addView(wideButton("Limpiar selección", v ->
                runEngine("Selección despejada", MainActivity::nativeClearSelection, true)),
                new LinearLayout.LayoutParams(-1, dp(46)));
    }

    private View materialSlider(String label, int initial, java.util.function.IntConsumer setter) {
        LinearLayout group = vertical();
        TextView value = text(label + "  " + initial, 11.5f, MUTED);
        group.addView(value);
        SeekBar seek = new SeekBar(this);
        seek.setMax(255);
        seek.setProgress(initial);
        seek.setProgressTintList(android.content.res.ColorStateList.valueOf(ACCENT));
        seek.setThumbTintList(android.content.res.ColorStateList.valueOf(TEXT));
        seek.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onStartTrackingTouch(SeekBar bar) { }
            @Override public void onProgressChanged(SeekBar bar, int current, boolean fromUser) {
                setter.accept(current);
                value.setText(label + "  " + current);
            }
            @Override public void onStopTrackingTouch(SeekBar bar) { applyMaterial(); }
        });
        group.addView(seek, new LinearLayout.LayoutParams(-1, dp(38)));
        return group;
    }

    private void applyMaterial() {
        int preset = materialPreset, rough = roughness, metal = metallic;
        runEngine("Material PBR actualizado", () -> nativeSetMaterial(preset, rough, metal), true);
    }

    private View axisRow(String label, String negativeLabel, String positiveLabel,
                         EngineCall negative, EngineCall positive) {
        LinearLayout row = horizontal();
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(0, dp(2), 0, dp(2));
        row.addView(text(label, 11.5f, MUTED), widthWeight(1f));
        row.addView(wideButton(negativeLabel, v -> runEngine(label, negative, true)));
        row.addView(wideButton(positiveLabel, v -> runEngine(label, positive, true)));
        return row;
    }

    private EditText editorField(String hint, String value) {
        EditText field = new EditText(this);
        field.setHint(hint);
        field.setText(value);
        field.setTextColor(TEXT);
        field.setHintTextColor(Color.rgb(102, 117, 140));
        field.setTextSize(12f);
        field.setSingleLine(true);
        field.setPadding(dp(10), 0, dp(10), 0);
        field.setBackground(shape(SURFACE_2, dp(9), 1));
        return field;
    }

    private View labeledSpinner(String label, String[] values, int selected,
                                IntSetter setter) {
        LinearLayout row = horizontal();
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.addView(text(label, 11.5f, MUTED), widthWeight(1f));
        Spinner spinner = new Spinner(this);
        spinner.setAdapter(new ArrayAdapter<>(this,
                android.R.layout.simple_spinner_dropdown_item, values));
        spinner.setSelection(Math.max(0, Math.min(values.length - 1, selected)), false);
        spinner.setBackgroundTintList(android.content.res.ColorStateList.valueOf(ACCENT));
        spinner.setOnItemSelectedListener(new android.widget.AdapterView.OnItemSelectedListener() {
            private boolean first = true;
            @Override public void onNothingSelected(android.widget.AdapterView<?> parent) { }
            @Override public void onItemSelected(android.widget.AdapterView<?> parent, View view,
                                                  int position, long id) {
                if (first) { first = false; return; }
                setter.set(position);
            }
        });
        row.addView(spinner, new LinearLayout.LayoutParams(dp(154), dp(44)));
        return row;
    }

    private void applyComposition(String message) {
        byte[] title = compositionTitle.getBytes(StandardCharsets.UTF_8);
        byte[] artist = compositionArtist.getBytes(StandardCharsets.UTF_8);
        int background = compositionBackground;
        int shape = compositionShape;
        int layout = compositionLayout;
        int energy = compositionEnergy;
        runEngine(message, () -> nativeSetComposition(title, artist, background,
                shape, layout, energy), true);
    }

    private View sectionHeader(String titleValue, String description) {
        LinearLayout header = vertical();
        TextView title = text(titleValue, 18f, TEXT);
        title.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        header.addView(title);
        TextView subtitle = text(description, 11.5f, MUTED);
        subtitle.setPadding(0, dp(2), 0, dp(12));
        header.addView(subtitle);
        return header;
    }

    private TextView sectionLabel(String value) {
        TextView view = text(value, 10f, Color.rgb(127, 153, 255));
        view.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        view.setLetterSpacing(0.12f);
        view.setPadding(0, dp(6), 0, dp(5));
        return view;
    }

    private void selectTool(Tool tool) {
        activeTool = tool;
        addNodeArmed = false;
        updateToolStyles();
        updateViewportBadge();
        buildInspector();
        postStatus(toolDescription(), false);
    }

    private void updateToolStyles() {
        styleTool(composeToolButton, activeTool == Tool.COMPOSE);
        styleTool(cameraToolButton, activeTool == Tool.CAMERA);
        styleTool(objectToolButton, activeTool == Tool.OBJECT);
        styleTool(surfaceToolButton, activeTool == Tool.SURFACE);
        styleTool(nodeToolButton, activeTool == Tool.NODES);
    }

    private void styleTool(Button button, boolean selected) {
        if (button == null) return;
        button.setTextColor(selected ? TEXT : MUTED);
        button.setBackground(shape(selected ? ACCENT_DARK : Color.TRANSPARENT,
                dp(12), selected ? 1 : 0));
    }

    private void updateViewportBadge() {
        if (viewportBadge != null) viewportBadge.setText(toolHint());
    }

    private String toolTitle() {
        if (activeTool == Tool.COMPOSE) return "Composición audiovisual 2D";
        if (activeTool == Tool.CAMERA) return "Cámara de autoría";
        if (activeTool == Tool.OBJECT) return "Modelo y geometría";
        if (activeTool == Tool.SURFACE) return "Superficie y textura";
        return "Grafo de nodos 3D";
    }

    private String toolDescription() {
        if (activeTool == Tool.COMPOSE) return "Core, campo musical, fondo, HUD y timeline";
        if (activeTool == Tool.CAMERA) return "Órbita, paneo, dolly y encuadre del editor";
        if (activeTool == Tool.OBJECT) return "Transforma el modelo y su malla editable";
        if (activeTool == Tool.SURFACE) return "Material PBR y anclajes de superficie";
        return "Picking, anclajes, conexiones y manipulación directa";
    }

    private String toolHint() {
        if (activeTool == Tool.COMPOSE) return "CORE 2D · compositor real · arrastra para recorrer el tiempo";
        if (activeTool == Tool.CAMERA) return "CÁMARA · arrastra para orbitar · pellizca para dolly";
        if (activeTool == Tool.OBJECT) return "OBJETO · arrastra para mover · pellizca para escalar";
        if (activeTool == Tool.SURFACE) return "SUPERFICIE · edita el material PBR en el inspector";
        if (addNodeArmed) return "NODOS · toca la superficie para insertar";
        return "NODOS · arrastra un anclaje para manipular";
    }

    private void runEngine(String successMessage, EngineCall operation, boolean renderAfter) {
        if (destroyed) return;
        if (!engineReady) {
            postStatus("El motor todavía se está iniciando", false);
            return;
        }
        try {
            engineExecutor.execute(() -> {
                if (destroyed || !engineReady) return;
                int status = operation.run();
                if (status == 0) {
                    statsMessage = nativeSummary();
                    postStatus(successMessage, false);
                    if (renderAfter) requestFrame();
                } else postStatus("Operación rechazada · " + statusName(status), true);
            });
        } catch (java.util.concurrent.RejectedExecutionException ignored) {
            // Activity destruction won the race; the native session is already closing.
        }
    }

    private void requestFrame() {
        if (destroyed || !engineReady) return;
        frameDirty.set(true);
        if (!frameScheduled.compareAndSet(false, true)) return;
        try {
            engineExecutor.execute(() -> {
                frameDirty.set(false);
                if (destroyed || !engineReady) {
                    frameScheduled.set(false);
                    return;
                }
                int width = Math.max(1, requestedWidth);
                int height = Math.max(1, requestedHeight);
                int status = 0;
                boolean compose = activeTool == Tool.COMPOSE;
                int[] pixels;
                int[] nodePoints;
                if (compose) {
                    pixels = nativeRenderComposition(width, height,
                            compositionSample, COMPOSITION_DURATION);
                    nodePoints = null;
                } else {
                    if (width != nativeWidth || height != nativeHeight) {
                        status = nativeResize(width, height);
                        if (status == 0) { nativeWidth = width; nativeHeight = height; }
                    }
                    pixels = status == 0 ? nativeRender() : null;
                    nodePoints = pixels != null ? nativeNodePoints() : null;
                }
                if (pixels == null || pixels.length != width * height) {
                    postStatus(status == 0 ? "El motor no produjo el frame" :
                            "No se pudo redimensionar · " + statusName(status), true);
                } else {
                    statsMessage = nativeSummary();
                    runOnUiThread(() -> {
                        if (destroyed || engineView == null) return;
                        engineView.acceptFrame(width, height, pixels, nodePoints);
                        if (statsView != null) statsView.setText(statsMessage);
                    });
                }
                frameScheduled.set(false);
                if (frameDirty.get()) requestFrame();
            });
        } catch (java.util.concurrent.RejectedExecutionException ignored) {
            frameScheduled.set(false);
        }
    }

    private void postStatus(String message, boolean error) {
        statusMessage = message;
        runOnUiThread(() -> {
            if (destroyed || statusView == null) return;
            statusView.setText(message);
            statusView.setTextColor(error ? DANGER : MUTED);
            if (statsView != null) statsView.setText(statsMessage);
        });
    }

    private void chooseObj() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, OPEN_OBJ_REQUEST);
    }

    private void chooseCoreImage() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("image/*");
        startActivityForResult(intent, OPEN_CORE_IMAGE_REQUEST);
    }

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if ((requestCode != OPEN_OBJ_REQUEST && requestCode != OPEN_CORE_IMAGE_REQUEST)
                || resultCode != RESULT_OK || data == null) return;
        Uri uri = data.getData();
        if (uri == null) return;
        if (requestCode == OPEN_CORE_IMAGE_REQUEST) {
            importCoreImage(uri);
            return;
        }
        postStatus("Leyendo OBJ…", false);
        new Thread(() -> {
            try (InputStream input = getContentResolver().openInputStream(uri)) {
                if (input == null) throw new IOException("No se pudo abrir el archivo");
                byte[] bytes = readFully(input);
                runEngine("OBJ importado · modelo enfocado", () -> nativeImportObj(bytes), true);
            } catch (IOException | OutOfMemoryError error) {
                postStatus("No se pudo leer el OBJ · " + error.getMessage(), true);
            }
        }, "odpar-obj-reader").start();
    }

    private void importCoreImage(Uri uri) {
        postStatus("Decodificando imagen central…", false);
        new Thread(() -> {
            try (InputStream input = getContentResolver().openInputStream(uri)) {
                if (input == null) throw new IOException("No se pudo abrir la imagen");
                Bitmap decoded = BitmapFactory.decodeStream(input);
                if (decoded == null) throw new IOException("Formato de imagen no admitido");
                int width = decoded.getWidth(), height = decoded.getHeight();
                long pixelCount = (long)width * height;
                if (pixelCount <= 0L || pixelCount > 67_108_864L)
                    throw new IOException("La imagen excede el presupuesto seguro");
                int[] argb = new int[(int)pixelCount];
                decoded.getPixels(argb, 0, width, 0, 0, width, height);
                byte[] rgba = new byte[(int)pixelCount * 4];
                for (int i = 0, o = 0; i < argb.length; ++i) {
                    int c = argb[i];
                    rgba[o++] = (byte)((c >>> 16) & 255);
                    rgba[o++] = (byte)((c >>> 8) & 255);
                    rgba[o++] = (byte)(c & 255);
                    rgba[o++] = (byte)((c >>> 24) & 255);
                }
                decoded.recycle();
                runEngine("Imagen central importada", () ->
                        nativeSetCoreImage(rgba, width, height), true);
            } catch (IOException | OutOfMemoryError error) {
                postStatus("No se pudo importar la imagen · " + error.getMessage(), true);
            }
        }, "odpar-core-image-reader").start();
    }

    private static byte[] readFully(InputStream input) throws IOException {
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        byte[] block = new byte[64 * 1024];
        for (;;) {
            int count = input.read(block);
            if (count < 0) break;
            if (count > 0) output.write(block, 0, count);
        }
        return output.toByteArray();
    }

    private static String statusName(int status) {
        String[] names = { "OK", "INVALID_ARGUMENT", "UNSUPPORTED", "OVERFLOW",
                "OUT_OF_MEMORY", "BUDGET_EXCEEDED", "CANCELLED", "BUSY", "INVALID_STATE",
                "BUFFER_TOO_SMALL", "INVARIANT_BROKEN", "INTERNAL_ERROR", "INVALID_DATA",
                "INTEGRITY_ERROR", "VERSION_MISMATCH" };
        return status >= 0 && status < names.length ? names[status] : "STATUS_" + status;
    }

    private Button actionButton(String label, View.OnClickListener listener) {
        Button button = wideButton(label, listener);
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(-2, dp(42));
        p.setMargins(dp(4), 0, 0, 0);
        button.setLayoutParams(p);
        return button;
    }

    private Button toolButton(String label, View.OnClickListener listener) {
        Button button = new Button(this);
        button.setText(label);
        button.setTextSize(10f);
        button.setAllCaps(false);
        button.setGravity(Gravity.CENTER);
        button.setMinWidth(0);
        button.setMinimumWidth(0);
        button.setPadding(dp(2), 0, dp(2), 0);
        button.setOnClickListener(listener);
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(-1, dp(51));
        p.setMargins(0, dp(2), 0, dp(2));
        button.setLayoutParams(p);
        return button;
    }

    private Button wideButton(String label, View.OnClickListener listener) {
        Button button = new Button(this);
        button.setText(label);
        button.setTextColor(TEXT);
        button.setTextSize(11f);
        button.setAllCaps(false);
        button.setGravity(Gravity.CENTER);
        button.setMinWidth(0);
        button.setMinimumWidth(0);
        button.setPadding(dp(11), 0, dp(11), 0);
        button.setBackground(shape(SURFACE_2, dp(10), 1));
        button.setOnClickListener(listener);
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(-2, dp(40));
        p.setMargins(dp(3), dp(2), dp(3), dp(2));
        button.setLayoutParams(p);
        return button;
    }

    private Button disabledButton(String label) {
        Button button = wideButton(label, null);
        button.setEnabled(false);
        button.setTextColor(Color.rgb(92, 104, 124));
        return button;
    }

    private TextView text(String value, float sizeSp, int color) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(sizeSp);
        view.setTextColor(color);
        view.setGravity(Gravity.CENTER_VERTICAL);
        return view;
    }

    private LinearLayout horizontal() { LinearLayout v = new LinearLayout(this); v.setOrientation(LinearLayout.HORIZONTAL); return v; }
    private LinearLayout vertical() { LinearLayout v = new LinearLayout(this); v.setOrientation(LinearLayout.VERTICAL); return v; }
    private void addDivider(LinearLayout parent) {
        View divider = new View(this);
        divider.setBackgroundColor(STROKE);
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(-1, dp(1));
        p.setMargins(0, dp(10), 0, dp(7));
        parent.addView(divider, p);
    }
    private GradientDrawable shape(int color, int radius, int strokeWidth) {
        GradientDrawable d = new GradientDrawable();
        d.setColor(color);
        d.setCornerRadius(radius);
        if (strokeWidth > 0) d.setStroke(dp(strokeWidth), STROKE);
        return d;
    }
    private LinearLayout.LayoutParams widthWeight(float weight) { return new LinearLayout.LayoutParams(0, -1, weight); }
    private LinearLayout.LayoutParams heightWeight(float weight) { return new LinearLayout.LayoutParams(-1, 0, weight); }
    private LinearLayout.LayoutParams matchWrap() { return new LinearLayout.LayoutParams(-1, -2); }
    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }

    private final class EngineView extends View {
        private final Paint bitmapPaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
        private final Paint guidePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint nodePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Rect destination = new Rect();
        private final ScaleGestureDetector scaleDetector;
        private Bitmap bitmap;
        private int[] nodePoints;
        private int frameWidth = 1;
        private int frameHeight = 1;
        private float lastX, lastY, downX, downY;
        private long lastGestureSubmit;
        private boolean moved, manipulating;

        EngineView() {
            super(MainActivity.this);
            setBackgroundColor(Color.rgb(10, 14, 23));
            guidePaint.setStyle(Paint.Style.STROKE);
            guidePaint.setStrokeWidth(dp(1));
            guidePaint.setColor(Color.argb(80, 170, 188, 225));
            nodePaint.setStyle(Paint.Style.FILL);
            scaleDetector = new ScaleGestureDetector(MainActivity.this,
                    new ScaleGestureDetector.SimpleOnScaleGestureListener() {
                @Override public boolean onScale(ScaleGestureDetector detector) {
                    if (!gestureGate(32L)) return true;
                    if (activeTool == Tool.COMPOSE) return true;
                    float factor = Math.max(0.82f, Math.min(1.22f, detector.getScaleFactor()));
                    if (activeTool == Tool.OBJECT) {
                        runEngine("Escala del modelo", () -> nativeScaleModel(factor), true);
                    } else {
                        float forward = (factor - 1f) * 3.2f;
                        runEngine("Dolly de cámara", () -> nativeDolly(forward), true);
                    }
                    return true;
                }
            });
        }

        @Override protected void onSizeChanged(int width, int height, int oldWidth, int oldHeight) {
            super.onSizeChanged(width, height, oldWidth, oldHeight);
            if (width <= 0 || height <= 0) return;
            requestedWidth = width;
            requestedHeight = height;
            requestFrame();
        }

        void acceptFrame(int width, int height, int[] pixels, int[] points) {
            if (bitmap == null || bitmap.getWidth() != width || bitmap.getHeight() != height)
                bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
            bitmap.setPixels(pixels, 0, width, 0, 0, width, height);
            frameWidth = width;
            frameHeight = height;
            nodePoints = points;
            invalidate();
        }

        @Override protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            if (bitmap != null) {
                destination.set(0, 0, getWidth(), getHeight());
                canvas.drawBitmap(bitmap, null, destination, bitmapPaint);
            }
            float cx = getWidth() * 0.5f, cy = getHeight() * 0.5f;
            canvas.drawLine(cx - dp(8), cy, cx + dp(8), cy, guidePaint);
            canvas.drawLine(cx, cy - dp(8), cx, cy + dp(8), guidePaint);
            if (nodePoints != null) {
                for (int i = 0; i + 3 < nodePoints.length; i += 4) {
                    float x = nodePoints[i] * getWidth() / (float)Math.max(1, frameWidth);
                    float y = nodePoints[i + 1] * getHeight() / (float)Math.max(1, frameHeight);
                    boolean selected = nodePoints[i + 2] != 0;
                    nodePaint.setColor(selected ? Color.WHITE : ACCENT);
                    canvas.drawCircle(x, y, dp(selected ? 7 : 5), nodePaint);
                    nodePaint.setStyle(Paint.Style.STROKE);
                    nodePaint.setStrokeWidth(dp(2));
                    nodePaint.setColor(selected ? ACCENT : Color.argb(190, 230, 238, 255));
                    canvas.drawCircle(x, y, dp(selected ? 10 : 8), nodePaint);
                    nodePaint.setStyle(Paint.Style.FILL);
                }
            }
        }

        @Override public boolean onTouchEvent(MotionEvent event) {
            scaleDetector.onTouchEvent(event);
            int action = event.getActionMasked();
            if (action == MotionEvent.ACTION_DOWN) {
                getParent().requestDisallowInterceptTouchEvent(true);
                lastX = downX = event.getX();
                lastY = downY = event.getY();
                moved = manipulating = false;
                if (activeTool == Tool.NODES && !addNodeArmed) {
                    float x = toRenderX(event.getX()), y = toRenderY(event.getY());
                    manipulating = true;
                    runEngine("Manipulación iniciada", () -> nativeManipulationBegin(x, y), false);
                }
                return true;
            }
            if (action == MotionEvent.ACTION_MOVE && event.getPointerCount() == 1
                    && !scaleDetector.isInProgress()) {
                float x = event.getX(), y = event.getY();
                float dx = x - lastX, dy = y - lastY;
                lastX = x; lastY = y;
                if (Math.abs(x - downX) + Math.abs(y - downY) > dp(7)) moved = true;
                if (!gestureGate(30L)) return true;
                if (activeTool == Tool.COMPOSE) {
                    long delta = (long)(dx * COMPOSITION_DURATION /
                            Math.max(1f, getWidth()) * 0.35f);
                    compositionSample = Math.max(0L,
                            Math.min(COMPOSITION_DURATION, compositionSample + delta));
                    requestFrame();
                } else if (activeTool == Tool.CAMERA) {
                    float yaw = dx * 0.20f, pitch = dy * 0.20f;
                    runEngine("Orbitando cámara", () -> nativeOrbit(yaw, pitch), true);
                } else if (activeTool == Tool.OBJECT) {
                    float worldX = 3f * dx / Math.max(1, getWidth());
                    float worldY = -3f * dy / Math.max(1, getHeight());
                    runEngine("Moviendo modelo", () -> nativeMoveModel(worldX, worldY, 0f), true);
                } else if (activeTool == Tool.NODES && manipulating) {
                    float renderX = toRenderX(x), renderY = toRenderY(y);
                    runEngine("Manipulando nodo", () -> nativeManipulationUpdate(renderX, renderY), true);
                }
                return true;
            }
            if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
                getParent().requestDisallowInterceptTouchEvent(false);
                if (activeTool == Tool.NODES) {
                    if (addNodeArmed && !moved && action == MotionEvent.ACTION_UP) {
                        float x = toRenderX(event.getX()), y = toRenderY(event.getY());
                        addNodeArmed = false;
                        runEngine("Nodo de superficie creado", () ->
                                nativeAddSurfaceNode(x, y, connectNodes ? 1 : 0), true);
                        buildInspector();
                        updateViewportBadge();
                    } else if (manipulating) {
                        manipulating = false;
                        runEngine("Manipulación terminada", MainActivity::nativeManipulationEnd, true);
                    }
                }
                return true;
            }
            return true;
        }

        private boolean gestureGate(long intervalMs) {
            long now = SystemClock.uptimeMillis();
            if (now - lastGestureSubmit < intervalMs) return false;
            lastGestureSubmit = now;
            return true;
        }
        private float toRenderX(float x) { return x * requestedWidth / Math.max(1f, getWidth()); }
        private float toRenderY(float y) { return y * requestedHeight / Math.max(1f, getHeight()); }
    }
}
