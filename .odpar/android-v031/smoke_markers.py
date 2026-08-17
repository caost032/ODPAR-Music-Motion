from pathlib import Path
import sys
p = Path(sys.argv[1] if len(sys.argv) > 1 else 'android/app/src/main/java/com/odpar/musicmotionlab/MainActivity.java')
s = p.read_text()
def r(old,new,label):
    global s
    if s.count(old) != 1:
        raise SystemExit(f'smoke marker anchor {label} count={s.count(old)}')
    s = s.replace(old,new,1)
r('import android.provider.MediaStore;\n','import android.provider.MediaStore;\nimport android.util.Log;\n','import')
r('                String info = nativeEngineInfo();\n                ui.post(() -> engineBadge.setText(compactEngineBadge(info)));','                String info = nativeEngineInfo();\n                Log.i("ODPAR", "ODPAR_SMOKE_ENGINE_OK");\n                ui.post(() -> engineBadge.setText(compactEngineBadge(info)));','engine')
r('                if (frame != null) {\n                    lastBitmap = frame;','                if (frame != null) {\n                    Log.i("ODPAR", "ODPAR_SMOKE_FRAME_OK");\n                    lastBitmap = frame;','frame')
p.write_text(s)
print('ODPAR API29 smoke markers installed')
