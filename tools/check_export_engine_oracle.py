#!/usr/bin/env python3
"""Independent ODPAR 0.14 Export Engine oracle.

Reconstructs canonical Profile/Recipe bytes, SHA identities, frame->sample,
S32LE padding, and FFmpeg argv tokens without using the C implementation to
compute expected values. C is only the system under test.
"""
from __future__ import annotations
import argparse, hashlib, pathlib, struct, subprocess, tempfile
import check_layered_oracle as layered_oracle

PROFILE_BYTES=256
RECIPE_BYTES=512
EXPORT_POLICY_BYTES=1024
DOMAIN=b"ODPAR_EXPORT_RECIPE_V1\0"  # C sizeof(string literal) includes NUL
Q=2147483647
CFG_SHA=hashlib.sha256(layered_oracle.config_bytes()).digest()
POLICY_SHA=hashlib.sha256(layered_oracle.policy_bytes()).digest()

PROBE=r'''#include "odm_export.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t qr(uint32_t n,uint32_t d){return (uint32_t)(((uint64_t)INT32_MAX*n+d/2u)/d);}
static void hx(const uint8_t*p,size_t n){size_t i;for(i=0;i<n;++i)printf("%02x",p[i]);}
static void cfg(odm_layered_config*c){
 memset(c,0,sizeof(*c));c->schema_version=1;c->canvas.schema_version=1;c->canvas.aspect=ODM_CANVAS_ASPECT_SQUARE_1_1;c->canvas.width=32;c->canvas.height=32;c->canvas.fps=30;
 c->canvas.safe_left_q31=qr(1,20);c->canvas.safe_top_q31=qr(1,20);c->canvas.safe_right_q31=qr(1,20);c->canvas.safe_bottom_q31=qr(1,20);
 c->background.schema_version=1;c->background.style=ODM_BACKGROUND_GRID;c->background.solid_color.a=65535;c->background.grid_color.r=65535;c->background.grid_color.g=65535;c->background.grid_color.b=65535;c->background.grid_color.a=65535;c->background.grid_spacing_q16=8u<<16;c->background.grid_line_q16=1u<<16;c->background.grid_feather_q16=1u<<16;c->background.zoom_reactivity_q31=qr(1,4);c->background.warp_reactivity_q31=qr(1,8);c->background.depth_reactivity_q31=qr(1,8);c->background.opacity_q31=INT32_MAX;
 c->core.schema_version=1;c->core.shape=ODM_CORE_SHAPE_CIRCLE;c->core.fit=ODM_CORE_FIT_COVER;c->core.center_x_q31=qr(1,2);c->core.center_y_q31=qr(1,2);c->core.width_q31=qr(1,2);c->core.height_q31=qr(1,2);c->core.corner_radius_q31=qr(1,8);c->core.border_q16=1u<<16;c->core.feather_q16=1u<<16;c->core.scale_reactivity_q31=qr(1,16);c->core.opacity_q31=INT32_MAX;c->core.border_color.r=65535;c->core.border_color.g=65535;c->core.border_color.b=65535;c->core.border_color.a=65535;
 c->field.schema_version=1;c->field.flags=ODM_FIELD_RADIAL_BARS|ODM_FIELD_PARTICLES|ODM_FIELD_ORBIT_RING;c->field.radial_segments=48;c->field.particle_count=8;c->field.ring_gap_q16=2u<<16;c->field.bar_min_q16=1u<<16;c->field.bar_max_q16=4u<<16;c->field.bar_width_q16=1u<<16;c->field.particle_radius_q16=1u<<16;c->field.field_opacity_q31=qr(3,4);c->field.primary_color.r=65535;c->field.primary_color.g=65535;c->field.primary_color.b=65535;c->field.primary_color.a=65535;c->field.secondary_color.r=32767;c->field.secondary_color.g=32767;c->field.secondary_color.b=32767;c->field.secondary_color.a=65535;c->field.seed=UINT64_C(0x0d130d130d130d13);
 /* Mismos valores que la sonda del oraculo de capas: los dos reconstruyen la
  * MISMA configuracion, asi que tienen que pedirla igual. */
 c->field.bar_shape=ODM_FIELD_BAR_WEDGE;c->field.particle_depth_q31=qr(3,4);
 c->field.layout=ODM_FIELD_LAYOUT_WAVE;c->field.particle_shape=ODM_FIELD_PARTICLE_LEAF;
 c->field.particle_nature=ODM_PARTICLE_NATURE_SWIRL;c->field.particle_flow_q31=qr(1,5);
 c->field.particle_drag_q31=qr(7,10);c->field.particle_impulse_q31=qr(1,2);
 c->field.particle_flutter_q31=qr(2,5);
 c->hud.schema_version=1;c->hud.flags=ODM_HUD_PROGRESS_BAR|ODM_HUD_TIME_CODE|ODM_HUD_TITLE|ODM_HUD_ARTIST;c->hud.margin_q16=2u<<16;c->hud.progress_height_q16=1u<<16;c->hud.progress_width_q31=qr(3,4);c->hud.text_scale_q16=1u<<16;c->hud.line_gap_q16=1u<<16;c->hud.opacity_q31=INT32_MAX;c->hud.foreground_color.r=65535;c->hud.foreground_color.g=65535;c->hud.foreground_color.b=65535;c->hud.foreground_color.a=65535;c->hud.background_color.a=65535;memcpy(c->hud.title,"QUIET, NOT EMPTY",17);memcpy(c->hud.artist,"AFTERIMAGE",11);
}
static void prof(odm_export_profile*p){memset(p,0,sizeof(*p));p->schema_version=1;p->adapter=1;p->container=1;p->video_codec=1;p->audio_codec=1;p->source_video_format=1;p->source_audio_format=1;p->output_pixel_format=1;p->color_primaries=1;p->color_transfer=1;p->color_matrix=1;p->color_range=1;p->rate_control=1;p->crf=18;p->preset=2;p->video_threads=4;p->audio_bitrate_kbps=256;p->flags=1;}
int main(void){odm_layered_config c;odm_export_plan lp;odm_export_profile p;odm_export_recipe r;uint8_t eb[1024],pb[256],rb[512],ab[40];uint64_t n=0;uint32_t tc=0;char av[4096];int64_t s=0;odm_pcm_stereo_q31 *pcm;uint64_t i,pos;
 cfg(&c);prof(&p);if(odm_export_plan_build(&c,48001,48001,ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,&lp))return 2;if(odm_export_recipe_build(&lp,&p,&r))return 3;
 if(odm_export_policy_bytes(eb,sizeof(eb),&n)||n!=1024){return 4;}
 printf("E,");hx(eb,1024);putchar('\n');
 if(odm_export_profile_bytes(&p,pb,sizeof(pb),&n)||n!=256){return 5;}
 printf("P,");hx(pb,256);putchar('\n');
 if(odm_export_recipe_bytes(&r,rb,sizeof(rb),&n)||n!=512){return 5;}
 printf("R,");hx(rb,512);putchar('\n');
 if(odm_export_frame_sample(&r,30,&s)){return 6;}
 printf("S,%lld\n",(long long)s);
 pcm=(odm_pcm_stereo_q31*)calloc((size_t)r.audio_source_frames,sizeof(*pcm));
 if(!pcm){return 7;}
 pcm[r.audio_source_frames-2].left=INT32_MAX;pcm[r.audio_source_frames-2].right=INT32_MIN;pcm[r.audio_source_frames-1].left=123;pcm[r.audio_source_frames-1].right=-456;
 if(odm_export_audio_chunk_s32le(&r,pcm,r.audio_source_frames,r.audio_source_frames-2,5,ab,sizeof(ab),&n)||n!=40){free(pcm);return 8;}
 free(pcm);printf("A,");hx(ab,40);putchar('\n');
 if(odm_export_ffmpeg_argv_template(&r,av,sizeof(av),&n,&tc)){return 9;}
 printf("N,%u,%llu\n",tc,(unsigned long long)n);printf("V,");for(i=0,pos=0;i<tc;i++){const char*t=av+pos;if(i)putchar('|');fputs(t,stdout);pos+=(uint64_t)strlen(t)+1u;}putchar('\n');
 {uint32_t offs[64];if(odm_export_ffmpeg_argv_bind(&r,"/tmp/video input.rgba","/tmp/audio input.s32le","/tmp/output file.mp4",av,sizeof(av),&n,&tc,offs,64u))return 10;printf("B,");for(i=0;i<tc;i++){if(i)putchar('|');fputs(av+offs[i],stdout);}putchar('\n');}
 return 0;}
'''

def u32(v): return struct.pack('<I',v & 0xffffffff)
def i32(v): return struct.pack('<i',v)
def u64(v): return struct.pack('<Q',v)
def i64(v): return struct.pack('<q',v)

def export_policy_bytes():
    b=bytearray(b'ODMEXPO1')
    vals=[
      1,1,PROFILE_BYTES,RECIPE_BYTES,2,512,4096,64,
      1,1,1,1,1,1,1,1,1,1,1,1,
      1,2,3,4,1,
      48000,2,8,1024,
      1,1,1,1,1,1,1,1,1,
      1,1,1,1,1,
    ]
    for v in vals:b+=u32(v)
    b+=b'ODPAR_EXPORT_RECIPE_V1\0'
    b+=b'ODPAR_EXPORT_RGBA8_STREAM_V1\0'
    b+=b'ODPAR_EXPORT_S32LE_STREAM_V1\0'
    b+=b'ODPAR_EXPORT_CANONICAL_PCM_Q31_SOURCE_V1\0'
    b+=b'ODPAR_EXPORT_RUN_RECEIPT_V1\0'
    argv=(
      b'ffmpeg\0-hide_banner\0-loglevel\0error\0-nostdin\0-n\0-f\0rawvideo\0'
      b'-pixel_format\0rgba\0-video_size\0<WIDTHxHEIGHT>\0'
      b'-framerate\0<FPS>\0-i\0{ODPAR_VIDEO_RGBA8}\0'
      b'-f\0s32le\0-ar\048000\0-ac\02\0-i\0{ODPAR_AUDIO_S32LE}\0'
      b'-map\00:v:0\0-map\01:a:0\0-map_metadata\0-1\0-map_chapters\0-1\0'
      b'-c:v\0libx264\0-preset\0<PRESET>\0-crf\0<CRF>\0'
      b'-threads\0<THREADS>\0-pix_fmt\0yuv420p\0-colorspace\0bt709\0'
      b'-color_primaries\0bt709\0-color_trc\0iec61966-2-1\0'
      b'-color_range\0tv\0-c:a\0aac\0-b:a\0<ABR>\0'
      b'-frames:v\0<FRAME_COUNT>\0[-movflags\0+faststart]\0'
      b'{ODPAR_OUTPUT_MP4}\0\0'
    )
    b+=argv
    assert len(b)<=EXPORT_POLICY_BYTES,len(b)
    b+=bytes(EXPORT_POLICY_BYTES-len(b))
    return bytes(b)

def profile_bytes():
    b=bytearray(b'ODMEXPF1')
    vals=[1,1,1,1,1,1,1,1,1,1,1,1,1,18,2,4,256,1] + [0]*10
    for v in vals:b+=u32(v)
    assert len(b)==120
    b+=bytes(PROFILE_BYTES-len(b))
    return bytes(b)

def recipe_bytes(recipe_sha:bytes):
    ph=hashlib.sha256(profile_bytes()).digest()
    b=bytearray(b'ODMEXPR1')
    vals=[1,1,1,1,1,1,1,1,32,32,30,48000,2,18,2,4,256,1]
    for idx,v in enumerate(vals): b += i32(v) if idx==10 else u32(v)
    # 31 frames, spf 1600, project 48001, output 49600, pad 1599
    b+=u64(31)+i64(1600)+i64(48001)+i64(49600)
    b+=u64(48001)+u64(1599)+u64(4096)+u64(126976)+u64(49600)+u64(396800)
    eph=hashlib.sha256(export_policy_bytes()).digest()
    b+=POLICY_SHA+CFG_SHA+ph+recipe_sha+eph
    assert len(b)==320,len(b)
    b+=bytes(RECIPE_BYTES-len(b))
    return bytes(b)

def expected_recipe():
    zero=recipe_bytes(bytes(32))
    rid=hashlib.sha256(DOMAIN+zero).digest()
    return recipe_bytes(rid),rid

def expected_audio():
    # Two real stereo Q31 frames followed by three exact silent frames.
    b=bytearray()
    for l,r in [(2147483647,-2147483648),(123,-456),(0,0),(0,0),(0,0)]:
        b+=struct.pack('<ii',l,r)
    return bytes(b)

def expected_argv():
    return [
      'ffmpeg','-hide_banner','-loglevel','error','-nostdin','-n','-f','rawvideo','-pixel_format','rgba',
      '-video_size','32x32','-framerate','30','-i','{ODPAR_VIDEO_RGBA8}',
      '-f','s32le','-ar','48000','-ac','2','-i','{ODPAR_AUDIO_S32LE}',
      '-map','0:v:0','-map','1:a:0','-map_metadata','-1','-map_chapters','-1',
      '-c:v','libx264','-preset','veryfast','-crf','18','-threads','4',
      '-pix_fmt','yuv420p','-colorspace','bt709','-color_primaries','bt709',
      '-color_trc','iec61966-2-1','-color_range','tv','-c:a','aac','-b:a','256k',
      '-frames:v','31','-movflags','+faststart','{ODPAR_OUTPUT_MP4}'
    ]

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--root',required=True);ap.add_argument('--cc',default='gcc');ap.add_argument('--library',required=True);a=ap.parse_args()
    root=pathlib.Path(a.root).resolve();lib=pathlib.Path(a.library).resolve()
    with tempfile.TemporaryDirectory(prefix='odm-export-oracle-') as td:
        td=pathlib.Path(td); src=td/'probe.c'; exe=td/'probe'; src.write_text(PROBE+'\n')
        cmd=[a.cc,'-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-I',str(root/'include'),str(src),str(lib),'-pthread','-lpng16','-lssl','-lcrypto','-o',str(exe)]
        c=subprocess.run(cmd,cwd=root,capture_output=True,text=True)
        if c.returncode: raise SystemExit('export oracle probe compile failed\n'+c.stdout+c.stderr)
        x=subprocess.run([str(exe)],cwd=root,capture_output=True,text=True)
        if x.returncode: raise SystemExit(f'export oracle probe failed rc={x.returncode}\n{x.stdout}\n{x.stderr}')
    rows={ln.split(',',1)[0]:ln.split(',',1)[1] for ln in x.stdout.splitlines() if ',' in ln}
    epl=export_policy_bytes(); ep=profile_bytes(); er,rid=expected_recipe()
    ge=bytes.fromhex(rows['E']); gp=bytes.fromhex(rows['P']); gr=bytes.fromhex(rows['R'])
    if ge!=epl:
        j=next(i for i,(x,y) in enumerate(zip(ge,epl)) if x!=y);raise SystemExit(f'export policy bytes mismatch at {j}: got={ge[j]} exp={epl[j]}')
    if gp!=ep:
        j=next(i for i,(x,y) in enumerate(zip(gp,ep)) if x!=y);raise SystemExit(f'profile bytes mismatch at {j}: got={gp[j]} exp={ep[j]}')
    if gr!=er:
        j=next(i for i,(x,y) in enumerate(zip(gr,er)) if x!=y);raise SystemExit(f'recipe bytes mismatch at {j}: got={gr[j]} exp={er[j]}')
    if int(rows['S'])!=48000:raise SystemExit('frame->sample mismatch')
    if bytes.fromhex(rows['A'])!=expected_audio():raise SystemExit('audio padding S32LE mismatch')
    tc,nb=map(int,rows['N'].split(',')); av=rows['V'].split('|'); ev=expected_argv()
    if av!=ev:raise SystemExit(f'argv token mismatch\n got={av}\n exp={ev}')
    exp_nb=sum(len(x.encode())+1 for x in ev)
    if tc!=len(ev) or nb!=exp_nb:raise SystemExit(f'argv counts mismatch got={tc}/{nb} exp={len(ev)}/{exp_nb}')
    if '-shortest' in av:raise SystemExit('forbidden -shortest present')
    bv=rows['B'].split('|')
    bmap={'{ODPAR_VIDEO_RGBA8}':'/tmp/video input.rgba','{ODPAR_AUDIO_S32LE}':'/tmp/audio input.s32le','{ODPAR_OUTPUT_MP4}':'/tmp/output file.mp4'}
    bev=[bmap.get(t,t) for t in ev]
    if bv!=bev:raise SystemExit(f'bound argv mismatch\n got={bv}\n exp={bev}')
    print('EXPORT ENGINE ORACLE: PASS')
    print(f'  export policy: {EXPORT_POLICY_BYTES} bytes sha256={hashlib.sha256(epl).hexdigest()}')
    print(f'  profile: {PROFILE_BYTES} bytes sha256={hashlib.sha256(ep).hexdigest()}')
    print(f'  recipe: {RECIPE_BYTES} bytes sha256={hashlib.sha256(er).hexdigest()}')
    print(f'  recipe identity: {rid.hex()}')
    print('  frame 30 -> sample 48,000 exact')
    print('  S32LE tail: 2 source frames + 3 exact silent frames')
    print(f'  ffmpeg argv: {len(ev)} shell-free tokens / {exp_nb} bytes / no -shortest / explicit stream maps')
    return 0
if __name__=='__main__':raise SystemExit(main())
