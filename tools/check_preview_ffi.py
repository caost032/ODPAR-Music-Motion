#!/usr/bin/env python3
"""Gate-8 preview ABI golden + dynamic shared-library lifecycle oracle."""
from __future__ import annotations
import argparse, ctypes, json, pathlib, subprocess, tempfile

STRICT=['-std=c11','-Wall','-Wextra','-Wpedantic','-Wconversion','-Wsign-conversion','-Wshadow','-Wstrict-prototypes','-Wmissing-prototypes','-Werror','-O2']
FIELDS={
'odm_ffi_preview_abi_info':['struct_size','schema_version','ffi_abi_version','endian_marker','preview_config_size','preview_asset_resource_size','preview_asset_frame_size','preview_plan_size','preview_frame_info_size','progress_snapshot_size','job_storage_alignment','music_analysis_tick_size','job_storage_bytes','feature_bits','reserved_u64'],
'odm_progress_snapshot':['schema_version','state','phase','error_code','cancel_requested','progress_millionths','reserved','generation','work_done','work_total'],
'odm_music_analysis_tick':['tick_index','center_sample','rms_left_q31','rms_right_q31','rms_mid_q31','rms_side_q31','band_energy_q31','total_energy_q31','envelope_short_q31','envelope_medium_q31','envelope_long_q31','spectral_flux_q31','spectral_centroid_bin_q16_16','energy_delta_q31','silence','stereo_balance_q31','stereo_width_q31','reserved'],
'odm_preview_config':['schema_version','raster_class','pixel_format','flags','reserved'],
'odm_preview_asset_resource':['resource_id','kind','frame_count','first_frame','flags','source_sha256'],
'odm_preview_asset_frame':['width','height','pixel_format','primaries','transfer','alpha_mode','flags','start_sample','end_sample','pixel_offset','pixel_bytes','reserved'],
'odm_preview_plan':['schema_version','raster_class','pixel_format','semantics_class','source_width','source_height','output_width','output_height','divisor','fps','frame_index','sample','canonical_frame_bytes','output_frame_bytes','scratch_bytes','work_units','render_ir_sha256','reserved'],
'odm_preview_frame_info':['schema_version','raster_class','pixel_format','semantics_class','source_width','source_height','output_width','output_height','fps','flags','frame_index','sample','output_bytes','canonical_frame_sha256','preview_pixel_sha256']}

def source():
    lines=['#include "odm_ffi.h"','#include <stddef.h>','#include <stdio.h>','int main(void){']
    for t,fs in FIELDS.items():
        lines.append(f'printf("SIZE {t} %zu\\n",sizeof({t}));')
        for f in fs: lines.append(f'printf("OFFSET {t} {f} %zu\\n",offsetof({t},{f}));')
    lines+=['return 0;}']
    return '\n'.join(lines)+'\n'

def parse(text):
    out={}
    for ln in text.splitlines():
        p=ln.split()
        if p[0]=='SIZE': out.setdefault(p[1],{'fields':{}})['size']=int(p[2])
        elif p[0]=='OFFSET': out.setdefault(p[1],{'fields':{}})['fields'][p[2]]=int(p[3])
        else: raise RuntimeError(f'bad probe line {ln!r}')
    return out

class PreviewAbi(ctypes.Structure):
    _fields_=[('struct_size',ctypes.c_uint32),('schema_version',ctypes.c_uint32),('ffi_abi_version',ctypes.c_uint32),('endian_marker',ctypes.c_uint32),('preview_config_size',ctypes.c_uint32),('preview_asset_resource_size',ctypes.c_uint32),('preview_asset_frame_size',ctypes.c_uint32),('preview_plan_size',ctypes.c_uint32),('preview_frame_info_size',ctypes.c_uint32),('progress_snapshot_size',ctypes.c_uint32),('job_storage_alignment',ctypes.c_uint32),('music_analysis_tick_size',ctypes.c_uint32),('job_storage_bytes',ctypes.c_uint64),('feature_bits',ctypes.c_uint64),('reserved_u64',ctypes.c_uint64*8)]
class BaseAbi(ctypes.Structure):
    _fields_=[('u32',ctypes.c_uint32*12),('feature_bits',ctypes.c_uint64),('reserved',ctypes.c_uint64)]
class Progress(ctypes.Structure):
    _fields_=[('schema_version',ctypes.c_uint32),('state',ctypes.c_uint32),('phase',ctypes.c_int32),('error_code',ctypes.c_int32),('cancel_requested',ctypes.c_uint32),('progress_millionths',ctypes.c_uint32),('reserved',ctypes.c_uint32),('generation',ctypes.c_uint64),('work_done',ctypes.c_uint64),('work_total',ctypes.c_uint64)]

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--root',required=True);ap.add_argument('--cc',default='gcc');ap.add_argument('--golden',required=True);ap.add_argument('--shared',required=True);a=ap.parse_args()
    root=pathlib.Path(a.root).resolve();gold=json.loads((root/a.golden).read_text());so=(root/a.shared).resolve()
    with tempfile.TemporaryDirectory(prefix='odm-preview-abi-') as td:
        td=pathlib.Path(td); c=td/'p.c'; exe=td/'p'; c.write_text(source(),newline='\n')
        r=subprocess.run([a.cc,*STRICT,'-I',str(root/'include'),str(c),'-o',str(exe)],cwd=root,capture_output=True,text=True)
        if r.returncode: raise SystemExit('preview ABI probe compile failed\n'+r.stdout+r.stderr)
        x=subprocess.run([str(exe)],cwd=root,capture_output=True,text=True,check=True)
    actual=parse(x.stdout)
    if actual!=gold['layouts']: raise SystemExit('preview ABI native layouts differ from golden')
    lib=ctypes.CDLL(str(so))
    q=lib.odm_ffi_preview_abi_query;q.argtypes=[ctypes.c_uint32,ctypes.POINTER(PreviewAbi),ctypes.c_uint64,ctypes.POINTER(ctypes.c_uint64)];q.restype=ctypes.c_int32
    info=PreviewAbi(); req=ctypes.c_uint64()
    if q(gold['schema_version'],ctypes.byref(info),ctypes.sizeof(info),ctypes.byref(req))!=0: raise SystemExit('dynamic preview ABI query failed')
    if (req.value,info.struct_size,info.schema_version,info.ffi_abi_version,info.endian_marker,info.feature_bits)!=(128,128,gold['schema_version'],gold['ffi_abi_version'],gold['endian_marker'],gold['runtime_feature_bits']): raise SystemExit('dynamic preview ABI values mismatch')
    if info.music_analysis_tick_size != gold['layouts']['odm_music_analysis_tick']['size'] or info.progress_snapshot_size != gold['layouts']['odm_progress_snapshot']['size']: raise SystemExit('dynamic preview dependent-layout sizes mismatch')
    for symbol in ('odm_ffi_preview_plan_frame','odm_ffi_preview_render_frame'):
        try: getattr(lib,symbol)
        except AttributeError as exc: raise SystemExit(f'missing dynamic preview symbol: {symbol}') from exc
    # Base ABI remains v1 feature-bits 31.
    bq=lib.odm_ffi_abi_query;bq.argtypes=[ctypes.c_uint32,ctypes.POINTER(BaseAbi),ctypes.c_uint64,ctypes.POINTER(ctypes.c_uint64)];bq.restype=ctypes.c_int32
    bi=BaseAbi(); br=ctypes.c_uint64()
    if bq(1,ctypes.byref(bi),ctypes.sizeof(bi),ctypes.byref(br))!=0 or bi.feature_bits!=31 or br.value!=64: raise SystemExit('base ABI was changed by preview extension')
    # Dynamic lifecycle through exported ABI only.
    raw=ctypes.create_string_buffer(info.job_storage_bytes+info.job_storage_alignment)
    addr=ctypes.addressof(raw); aligned=(addr+info.job_storage_alignment-1)&~(info.job_storage_alignment-1); ptr=ctypes.c_void_p(aligned)
    init=lib.odm_ffi_job_init;init.argtypes=[ctypes.c_void_p,ctypes.c_uint64];init.restype=ctypes.c_int32
    begin=lib.odm_ffi_job_begin;begin.argtypes=[ctypes.c_void_p,ctypes.c_uint64,ctypes.c_uint64,ctypes.POINTER(ctypes.c_uint64)];begin.restype=ctypes.c_int32
    snap=lib.odm_ffi_job_snapshot;snap.argtypes=[ctypes.c_void_p,ctypes.c_uint64,ctypes.POINTER(Progress)];snap.restype=ctypes.c_int32
    finish=lib.odm_ffi_job_finish;finish.argtypes=[ctypes.c_void_p,ctypes.c_uint64,ctypes.c_uint64,ctypes.c_int32];finish.restype=ctypes.c_int32
    destroy=lib.odm_ffi_job_destroy;destroy.argtypes=[ctypes.c_void_p,ctypes.c_uint64];destroy.restype=ctypes.c_int32
    gen=ctypes.c_uint64(); ps=Progress()
    if init(ptr,info.job_storage_bytes)!=0 or begin(ptr,info.job_storage_bytes,123,ctypes.byref(gen))!=0 or gen.value==0: raise SystemExit('dynamic caller-owned job begin failed')
    if snap(ptr,info.job_storage_bytes,ctypes.byref(ps))!=0 or ps.generation!=gen.value or ps.work_total!=123 or ps.state!=2: raise SystemExit('dynamic caller-owned job snapshot mismatch')
    if finish(ptr,info.job_storage_bytes,gen.value,0)!=0 or destroy(ptr,info.job_storage_bytes)!=0: raise SystemExit('dynamic caller-owned job finish/destroy failed')
    print(f'Preview FFI oracle: pass ({len(actual)} structs, shared ABI loaded, base ABI preserved, caller-owned lifecycle)')
    print(f'Preview job storage: {info.job_storage_bytes} bytes align {info.job_storage_alignment}; feature_bits={info.feature_bits}')
if __name__=='__main__': main()
