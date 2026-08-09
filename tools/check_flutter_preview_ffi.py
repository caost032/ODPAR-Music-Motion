#!/usr/bin/env python3
"""Check generated Flutter/Dart binding drift; runtime lane is conditional."""
from __future__ import annotations
import argparse, pathlib, shutil, subprocess, tempfile

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--root',required=True);ap.add_argument('--golden',required=True);ap.add_argument('--binding',required=True);a=ap.parse_args();root=pathlib.Path(a.root).resolve();binding=(root/a.binding).resolve();gold=(root/a.golden).resolve()
    with tempfile.TemporaryDirectory(prefix='odm-dart-ffi-') as td:
        tmp=pathlib.Path(td)/'binding.dart'
        subprocess.run(['python3',str(root/'tools/gen_flutter_preview_ffi.py'),'--golden',str(gold),'--output',str(tmp)],check=True,cwd=root)
        if binding.read_bytes()!=tmp.read_bytes(): raise SystemExit('Flutter FFI binding drift: regenerate odpar_music_ffi.dart')
    dart=shutil.which('dart'); flutter=shutil.which('flutter')
    if dart:
        r=subprocess.run([dart,'analyze',str(binding)],cwd=root,capture_output=True,text=True)
        if r.returncode: raise SystemExit('dart analyze failed\n'+r.stdout+r.stderr)
        print('Flutter FFI binding: pass (generated parity + dart analyze)')
    else:
        print('Flutter FFI binding: static parity pass; runtime/analyzer BLOCKED_MISSING_DART_TOOLCHAIN')
    if not flutter: print('Flutter runtime lane: BLOCKED_MISSING_FLUTTER_TOOLCHAIN')
    return 0
if __name__=='__main__':raise SystemExit(main())
