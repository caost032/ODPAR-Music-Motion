from pathlib import Path
import sys

root = Path(sys.argv[1] if len(sys.argv) > 1 else 'android')
app = root / 'app'
scene = app / 'src/main/cpp/odpar_lab_scene.c'
cmake = app / 'src/main/cpp/CMakeLists.txt'
main = app / 'src/main/java/com/odpar/musicmotionlab/MainActivity.java'
manifest = app / 'src/main/AndroidManifest.xml'
gradle = app / 'build.gradle'

def replace_once(path: Path, old: str, new: str, label: str):
    s = path.read_text()
    if old not in s:
        raise SystemExit(f'hotfix anchor missing: {label}: {path}')
    if s.count(old) != 1:
        raise SystemExit(f'hotfix anchor not unique: {label}: {path}: count={s.count(old)}')
    path.write_text(s.replace(old, new, 1))

# 1) Remove ~300 KiB immersive-world automatic storage from Android worker stack.
floor_type = '''typedef struct {\n    odm_vertex3d v[81];\n    odm_triangle3d t[128];\n    odm_mesh3d mesh;\n    odm_material3d material;\n    odm_asset3d asset;\n    odm_instance3d instance;\n} lab_floor;\n'''
replace_once(scene, floor_type, floor_type + '''\n/* Android v0.3.1: same authored world, heap-backed transient geometry. */\ntypedef struct {\n    lab_floor floor_o, back, left_wall, right_wall;\n    lab_box boxes[18];\n    lab_ring rings[2];\n} lab_world_geometry;\n''', 'world geometry type')
replace_once(scene,
'''    odm_camera3d camera;odm_lighting3d lighting;odm_raster3d_target target;odm_shadow3d_map shadow;\n    lab_floor floor_o,back,left_wall,right_wall;lab_box boxes[18];lab_ring rings[2];\n    uint32_t *depth=NULL,*shadow_depth=NULL;void *scratch=NULL;uint64_t scratch_bytes=0u,fragments=0u,sf=0u;''',
'''    odm_camera3d camera;odm_lighting3d lighting;odm_raster3d_target target;odm_shadow3d_map shadow;\n    lab_world_geometry *geo=NULL;\n    uint32_t *depth=NULL,*shadow_depth=NULL;void *scratch=NULL;uint64_t scratch_bytes=0u,fragments=0u,sf=0u;''', 'world local storage')
qv3 = '#define QV3(X,Y,Z) ((odm_vec3_q32){q_m((X)),q_m((Y)),q_m((Z))})\n'
replace_once(scene, qv3, qv3 + '''#define floor_o (geo->floor_o)\n#define back (geo->back)\n#define left_wall (geo->left_wall)\n#define right_wall (geo->right_wall)\n#define boxes (geo->boxes)\n#define rings (geo->rings)\n''', 'world aliases')
replace_once(scene,
'''    if(!rgba||width<64u||height<64u||width>1024u||height>1024u||camera_y_mm<200||camera_y_mm>8000||camera_x_mm<-12000||camera_x_mm>12000||camera_z_mm<-8000||camera_z_mm>30000){if(error&&error_cap)snprintf(error,error_cap,"invalid world arguments");return 0;}\n    depth=(uint32_t*)calloc((size_t)width*(size_t)height,sizeof(uint32_t));''',
'''    if(!rgba||width<64u||height<64u||width>1024u||height>1024u||camera_y_mm<200||camera_y_mm>8000||camera_x_mm<-12000||camera_x_mm>12000||camera_z_mm<-8000||camera_z_mm>30000){if(error&&error_cap)snprintf(error,error_cap,"invalid world arguments");return 0;}\n    geo=(lab_world_geometry*)calloc(1u,sizeof(*geo));\n    if(!geo){if(error&&error_cap)snprintf(error,error_cap,"world geometry allocation failed");return 0;}\n    depth=(uint32_t*)calloc((size_t)width*(size_t)height,sizeof(uint32_t));''', 'world heap allocation')
replace_once(scene,
'''    free(shadow_depth);free(depth);free(scratch);return 1;\nfail:\n    free(shadow_depth);free(depth);free(scratch);return 0;\n#undef QV3\n}''',
'''    free(shadow_depth);free(depth);free(scratch);free(geo);return 1;\nfail:\n    free(shadow_depth);free(depth);free(scratch);free(geo);return 0;\n#undef rings\n#undef boxes\n#undef right_wall\n#undef left_wall\n#undef back\n#undef floor_o\n#undef QV3\n}''', 'world heap release')

# 2) API 28/29 wrapper for the Linux memfd syscall. This removes reliance on
# Android libc's API-30 memfd_create wrapper while preserving OpenSSL surface.
compat = app / 'src/main/cpp/android_compat.c'
compat.write_text(r'''#define _GNU_SOURCE 1
#include <errno.h>
#include <sys/syscall.h>
#include <unistd.h>
int memfd_create(const char *name, unsigned int flags) {
#if defined(__NR_memfd_create)
    return (int)syscall(__NR_memfd_create, name, flags);
#else
    (void)name; (void)flags; errno = ENOSYS; return -1;
#endif
}
''')
replace_once(cmake, 'add_library(odpar_lab SHARED\n  native_bridge.c\n',
             'add_library(odpar_lab SHARED\n  android_compat.c\n  native_bridge.c\n', 'compat source')
replace_once(cmake,
             'set(OPENSSL_ANDROID_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openssl")',
             'set(OPENSSL_ANDROID_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openssl/${ANDROID_ABI}")',
             'per-ABI OpenSSL')

# 3) Never let a dynamic-loader error kill Activity class initialization.
replace_once(main,
'''public final class MainActivity extends Activity {\n    static { System.loadLibrary("odpar_lab"); }''',
'''public final class MainActivity extends Activity {\n    private static final Throwable NATIVE_LOAD_ERROR;\n    static {\n        Throwable e = null;\n        try { System.loadLibrary("odpar_lab"); } catch (Throwable t) { e = t; }\n        NATIVE_LOAD_ERROR = e;\n    }''', 'fail-safe native loader')
replace_once(main,
'    private final ExecutorService worker = Executors.newSingleThreadExecutor();',
'''    private final ExecutorService worker = Executors.newSingleThreadExecutor(r ->\n            new Thread(null, r, "ODPAR-Render", 8L * 1024L * 1024L));''', 'render thread stack')
replace_once(main,
'''        setImmersive();\n        setContentView(buildUi());\n        worker.execute(() -> {\n            String info = nativeEngineInfo();\n            ui.post(() -> engineBadge.setText(compactEngineBadge(info)));\n        });\n        ui.post(frameLoop);''',
'''        setImmersive();\n        if (NATIVE_LOAD_ERROR != null) {\n            setContentView(buildNativeFailureUi(NATIVE_LOAD_ERROR));\n            return;\n        }\n        setContentView(buildUi());\n        worker.execute(() -> {\n            try {\n                String info = nativeEngineInfo();\n                ui.post(() -> engineBadge.setText(compactEngineBadge(info)));\n            } catch (Throwable t) {\n                ui.post(() -> showRecoverableStartupError("nativeEngineInfo", t));\n            }\n        });\n        ui.post(frameLoop);''', 'safe onCreate')
replace_once(main, '    private void setImmersive() {', r'''    private View buildNativeFailureUi(Throwable t) {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        root.setPadding(dp(28), dp(24), dp(28), dp(24));
        root.setBackgroundColor(bg);
        TextView title = text("ODPAR · ARRANQUE NATIVO", 20, fg, true);
        title.setGravity(Gravity.CENTER);
        root.addView(title, llp(-1, -2));
        TextView msg = text("Android abrió el host, pero rechazó la biblioteca C.\n\n" +
                t.getClass().getName() + "\n" + String.valueOf(t.getMessage()) +
                "\n\nEste diagnóstico sustituye el cierre silencioso.",
                12, Color.rgb(220,190,190), false);
        msg.setTextIsSelectable(true);
        msg.setGravity(Gravity.CENTER);
        root.addView(msg, llp(-1, -2));
        return root;
    }

    private void showRecoverableStartupError(String where, Throwable t) {
        String detail = where + "\n" + t.getClass().getName() + "\n" + String.valueOf(t.getMessage());
        if (engineBadge != null) engineBadge.setText("C11 cargó · fallo recuperable");
        if (telemetry != null) telemetry.setText(detail);
        Toast.makeText(this, "ODPAR detectó un fallo recuperable.", Toast.LENGTH_LONG).show();
    }

    private void setImmersive() {''', 'diagnostic UI')

# Guard Java-level failures around the native frame call. Native signals are
# handled by the structural stack fix and API29 smoke test below.
replace_once(main,
'''            long t0 = System.nanoTime();\n            int[] pixels = nativeRenderWorld(size[0], size[1], cx, cy, cz, ya, pi, timeline, flags);\n            long t1 = System.nanoTime();\n            String meta = nativeLastError();''',
'''            long t0 = System.nanoTime();\n            int[] pixels;\n            String meta;\n            try {\n                pixels = nativeRenderWorld(size[0], size[1], cx, cy, cz, ya, pi, timeline, flags);\n                meta = nativeLastError();\n            } catch (Throwable t) {\n                ui.post(() -> {\n                    rendering.set(false); dirty = false; moveStrafe = moveForward = 0f;\n                    showRecoverableStartupError("nativeRenderWorld", t);\n                });\n                return;\n            }\n            long t1 = System.nanoTime();''', 'safe Java native frame call')

replace_once(manifest,
             'android:allowBackup="false" android:supportsRtl="true"',
             'android:allowBackup="false" android:supportsRtl="true" android:extractNativeLibs="true"',
             'native extraction')
replace_once(gradle, 'versionCode 3', 'versionCode 4', 'version code')
replace_once(gradle, "versionName '0.3.0-spatial-lab'", "versionName '0.3.1-spatial-lab'", 'version name')
replace_once(gradle, "ndk { abiFilters 'arm64-v8a' }", "ndk { abiFilters 'arm64-v8a', 'x86_64' }", 'x86 smoke ABI')

print('ODPAR Spatial Lab v0.3.1 crash hotfix applied')
