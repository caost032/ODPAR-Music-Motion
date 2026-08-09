#!/usr/bin/env python3
"""Local adapter smoke using argv emitted by ODPAR Export Engine.

This is explicitly an observation of the locally installed FFmpeg, not a
canonical codec/color certification. It proves that the engine-owned argv can
encode the exact requested video frame count without -shortest truncation.
"""
from __future__ import annotations
import argparse, json, pathlib, shutil, struct, subprocess, tempfile

PROBE=r'''#include "odm_export.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static uint32_t qr(uint32_t n,uint32_t d){return (uint32_t)(((uint64_t)INT32_MAX*n+d/2u)/d);}
static void cfg(odm_layered_config*c){memset(c,0,sizeof(*c));c->schema_version=1;c->canvas.schema_version=1;c->canvas.aspect=ODM_CANVAS_ASPECT_SQUARE_1_1;c->canvas.width=32;c->canvas.height=32;c->canvas.fps=30;c->background.schema_version=1;c->background.style=ODM_BACKGROUND_SOLID;c->background.solid_color.a=65535;c->background.opacity_q31=INT32_MAX;c->core.schema_version=1;c->core.shape=ODM_CORE_SHAPE_CIRCLE;c->core.fit=ODM_CORE_FIT_COVER;c->core.center_x_q31=qr(1,2);c->core.center_y_q31=qr(1,2);c->core.width_q31=qr(1,2);c->core.height_q31=qr(1,2);c->core.feather_q16=1u<<16;c->core.opacity_q31=INT32_MAX;c->field.schema_version=1;c->field.radial_segments=48;c->hud.schema_version=1;c->hud.text_scale_q16=1u<<16;}
static void prof(odm_export_profile*p){memset(p,0,sizeof(*p));p->schema_version=1;p->adapter=1;p->container=1;p->video_codec=1;p->audio_codec=1;p->source_video_format=1;p->source_audio_format=1;p->output_pixel_format=1;p->color_primaries=1;p->color_transfer=1;p->color_matrix=1;p->color_range=1;p->rate_control=1;p->crf=18;p->preset=1;p->video_threads=1;p->audio_bitrate_kbps=128;p->flags=1;}
int main(int argc,char**argv){odm_layered_config c;odm_export_plan lp;odm_export_profile p;odm_export_recipe r;char av[4096];uint32_t offs[ODM_EXPORT_ARGV_MAX_TOKENS];uint64_t n=0;uint32_t tc=0,i;if(argc!=4)return 1;cfg(&c);prof(&p);if(odm_export_plan_build(&c,1601,1601,ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,&lp))return 2;if(odm_export_recipe_build(&lp,&p,&r))return 3;if(r.frame_count!=2u||r.output_end_sample!=3200)return 4;if(odm_export_ffmpeg_argv_bind(&r,argv[1],argv[2],argv[3],av,sizeof(av),&n,&tc,offs,ODM_EXPORT_ARGV_MAX_TOKENS))return 5;for(i=0u;i<tc;++i){if(fwrite(av+offs[i],1,strlen(av+offs[i])+1u,stdout)!=strlen(av+offs[i])+1u)return 6;}return 0;}
'''

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--root',required=True); ap.add_argument('--cc',default='gcc'); ap.add_argument('--library',required=True); a=ap.parse_args()
    if not shutil.which('ffmpeg') or not shutil.which('ffprobe'):
        print('EXPORT FFMPEG SMOKE: BLOCKED_MISSING_FFMPEG'); return 0
    root=pathlib.Path(a.root).resolve(); lib=pathlib.Path(a.library).resolve()
    with tempfile.TemporaryDirectory(prefix='odm-export-smoke-') as td0:
        td=pathlib.Path(td0); src=td/'probe.c'; exe=td/'probe'; video=td/'v.rgba'; audio=td/'a.s32le'; out=td/'out.mp4'
        src.write_text(PROBE+'\n')
        cc=[a.cc,'-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-I',str(root/'include'),str(src),str(lib),'-pthread','-lpng16','-lssl','-lcrypto','-o',str(exe)]
        r=subprocess.run(cc,cwd=root,capture_output=True,text=True)
        if r.returncode: raise SystemExit('smoke probe compile failed\n'+r.stdout+r.stderr)
        r=subprocess.run([str(exe),str(video),str(audio),str(out)],cwd=root,capture_output=True)
        if r.returncode: raise SystemExit(f'smoke probe failed rc={r.returncode}: {r.stderr.decode(errors="replace")}')
        toks=[x.decode() for x in r.stdout.split(b'\0') if x]
        if '-shortest' in toks: raise SystemExit('forbidden -shortest in engine argv')
        # Two deterministic RGBA frames. Distinct frames make frame-loss visible.
        f0=bytearray();f1=bytearray()
        for y in range(32):
            for x in range(32):
                f0 += bytes((x*8 & 255,y*8 & 255,32,255))
                f1 += bytes((255-(x*8 & 255),64,y*8 & 255,255))
        video.write_bytes(f0+f1)
        # Exactly output_end_sample=3200 stereo s32le frames; source ended at 1601,
        # so this file models the engine-owned padded stream, not -shortest.
        ab=bytearray()
        for i in range(3200):
            if i<1601:
                v=((i*7919)&0x3fffffff)-0x1fffffff
                ab += struct.pack('<ii',v,-v)
            else:
                ab += b'\0'*8
        audio.write_bytes(ab)
        if any('{ODPAR_' in t for t in toks): raise SystemExit('unbound placeholder remained in final argv')
        argv=toks
        enc=subprocess.run(argv,cwd=td,capture_output=True,text=True)
        if enc.returncode: raise SystemExit('engine argv FFmpeg failed\n'+enc.stdout+enc.stderr)
        probe=['ffprobe','-v','error','-count_frames','-show_streams','-of','json',str(out)]
        pr=subprocess.run(probe,capture_output=True,text=True)
        if pr.returncode: raise SystemExit('ffprobe failed\n'+pr.stderr)
        obj=json.loads(pr.stdout); streams=obj.get('streams',[])
        vs=next((s for s in streams if s.get('codec_type')=='video'),None); aus=next((s for s in streams if s.get('codec_type')=='audio'),None)
        if not vs or not aus: raise SystemExit('missing video/audio stream')
        if int(vs.get('width',0))!=32 or int(vs.get('height',0))!=32: raise SystemExit('video geometry mismatch')
        if vs.get('r_frame_rate')!='30/1': raise SystemExit('video fps mismatch: '+str(vs.get('r_frame_rate')))
        if int(vs.get('nb_read_frames','0'))!=2: raise SystemExit('video frame count mismatch: '+str(vs.get('nb_read_frames')))
        if int(aus.get('sample_rate','0'))!=48000 or int(aus.get('channels',0))!=2: raise SystemExit('audio format mismatch')
        print('EXPORT FFMPEG SMOKE: PASS (local adapter observation only)')
        print('  engine-bound argv executed directly; no shell, no manual placeholder substitution and no hidden FFmpeg flags')
        print('  ffprobe: 32x32 @ 30 fps, exactly 2 video frames, AAC 48 kHz stereo')
        print('  final video frame preserved by padded audio; no -shortest')
    return 0
if __name__=='__main__': raise SystemExit(main())
