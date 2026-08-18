#!/usr/bin/env python3
from pathlib import Path
import base64,gzip,shutil
MANIFEST = {'app/src/main/java/com/odpar/musicmotion/MainActivity.java': ['MainActivity_java.00.part', 'MainActivity_java.01.part', 'MainActivity_java.02.part', 'MainActivity_java.03.part'], 'app/src/main/cpp/native_bridge.c': ['native_bridge_c.00.part', 'native_bridge_c.01.part', 'native_bridge_c.02.part'], 'app/src/main/cpp/CMakeLists.txt': ['CMakeLists_txt.00.part'], 'app/src/main/AndroidManifest.xml': ['AndroidManifest_xml.00.part'], 'app/build.gradle': ['build_gradle.00.part']}
base=Path("android_workspace_payload")
root=Path("android_engine_lab")
old=root/"app/src/main/java/com/odpar/musicmotionlab"
if old.exists(): shutil.rmtree(old)
for rel,parts in MANIFEST.items():
    data=''.join((base/p).read_text().strip() for p in parts)
    target=root/rel
    target.parent.mkdir(parents=True,exist_ok=True)
    target.write_bytes(gzip.decompress(base64.b64decode(data)))
print("materialized ODPAR Music Motion World Workspace sources")
