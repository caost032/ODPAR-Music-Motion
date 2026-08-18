#!/usr/bin/env python3
"""Harden the Android Engine Lab launch path.

The lab must always reach a visible Activity before touching the native engine.
A native loader/linkage problem is diagnostic information, not a reason for the
launcher Activity itself to disappear.
"""
from pathlib import Path

p = Path("android_engine_lab/app/src/main/java/com/odpar/musicmotionlab/MainActivity.java")
text = p.read_text(encoding="utf-8")

old_loader = '''public class MainActivity extends Activity {
    static { System.loadLibrary("odpar_lab"); }

    private native String nativeEngineInfo();'''
new_loader = '''public class MainActivity extends Activity {
    private static volatile boolean nativeLoaded = false;
    private static volatile String nativeLoadError = null;

    private static void tryLoadNative() {
        if (nativeLoaded || nativeLoadError != null) return;
        try {
            System.loadLibrary("odpar_lab");
            nativeLoaded = true;
        } catch (Throwable t) {
            nativeLoadError = t.getClass().getName() + ": " + String.valueOf(t.getMessage());
        }
    }

    private native String nativeEngineInfo();'''
if old_loader not in text:
    raise SystemExit("launch patch: static loader anchor not found")
text = text.replace(old_loader, new_loader, 1)

old_oncreate = '''        super.onCreate(savedInstanceState);
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
    }'''
new_oncreate = '''        super.onCreate(savedInstanceState);
        getWindow().setStatusBarColor(Color.rgb(7,9,13));
        getWindow().setNavigationBarColor(Color.rgb(7,9,13));

        // Reach a visible Java UI before executing any ODPAR native code.
        // This turns loader/linkage failures into visible diagnostics instead
        // of an Activity that appears to "not open".
        tryLoadNative();
        setContentView(buildUi());
        if (nativeLoaded) {
            engineBadge.setText("ODPAR native cargado");
            console.setText("La app abrió correctamente. Motor nativo cargado.\\n"
                    + "Pulsa RENDER para probar Scene3D o IDENTIDAD para consultar el Spine.");
            viewportStats.setText("Listo · esperando prueba explícita");
        } else {
            engineBadge.setText("APP ABIERTA · ERROR NATIVO");
            console.setText("La interfaz Android abrió correctamente, pero libodpar_lab.so no pudo cargarse.\\n\\n"
                    + String.valueOf(nativeLoadError)
                    + "\\n\\nEste error ya no puede cerrar silenciosamente la Activity.");
            viewportStats.setText("Loader nativo bloqueado · revisa Console / Evidence");
        }
    }'''
if old_oncreate not in text:
    raise SystemExit("launch patch: onCreate anchor not found")
text = text.replace(old_oncreate, new_oncreate, 1)

# Guard all entry points that can touch JNI. This also protects background
# worker threads from an UnsatisfiedLinkError when the loader failed.
request_anchor = '''    private void requestRender(boolean announce) {
'''
request_replacement = '''    private void requestRender(boolean announce) {
        if (!nativeLoaded) {
            if (console != null) console.setText("Motor nativo no disponible:\\n" + String.valueOf(nativeLoadError));
            if (viewportStats != null) viewportStats.setText("No se ejecutó JNI · la app sigue abierta");
            return;
        }
'''
if request_anchor not in text:
    raise SystemExit("launch patch: requestRender anchor not found")
text = text.replace(request_anchor, request_replacement, 1)

run_anchor = '''    private void runTask(String status, NativeTextTask task) {
        console.setText(status);
'''
run_replacement = '''    private void runTask(String status, NativeTextTask task) {
        if (!nativeLoaded) {
            console.setText("Motor nativo no disponible:\\n" + String.valueOf(nativeLoadError));
            return;
        }
        console.setText(status);
'''
if run_anchor not in text:
    raise SystemExit("launch patch: runTask anchor not found")
text = text.replace(run_anchor, run_replacement, 1)

p.write_text(text, encoding="utf-8")
print(f"patched launch path: {p}")
