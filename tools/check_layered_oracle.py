#!/usr/bin/env python3
"""Independent ODPAR layered-config/policy/frame-plan oracle (policy v12).

Expected bytes and fixed-point math are reconstructed here from literal contract
values.  The C implementation is used only as the observed system under test.
"""
from __future__ import annotations
import argparse, hashlib, pathlib, struct, subprocess, tempfile

Q=2147483647
CFG_BYTES=1024
POLICY_BYTES=1536
RADIAL=48

PROBE=r'''#include "odm_compositor.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static uint32_t qr(uint32_t n,uint32_t d){return (uint32_t)(((uint64_t)INT32_MAX*n+d/2u)/d);}
static void hx(const uint8_t*p,size_t n){size_t i;for(i=0;i<n;++i)printf("%02x",p[i]);}
static void cfg(odm_layered_config*c){
 uint32_t i;(void)i;memset(c,0,sizeof(*c));c->schema_version=1;
 c->canvas.schema_version=1;c->canvas.aspect=ODM_CANVAS_ASPECT_SQUARE_1_1;c->canvas.width=32;c->canvas.height=32;c->canvas.fps=30;
 c->canvas.safe_left_q31=qr(1,20);c->canvas.safe_top_q31=qr(1,20);c->canvas.safe_right_q31=qr(1,20);c->canvas.safe_bottom_q31=qr(1,20);
 c->background.schema_version=1;c->background.style=ODM_BACKGROUND_GRID;c->background.solid_color.a=65535;c->background.grid_color.r=65535;c->background.grid_color.g=65535;c->background.grid_color.b=65535;c->background.grid_color.a=65535;c->background.grid_spacing_q16=8u<<16;c->background.grid_line_q16=1u<<16;c->background.grid_feather_q16=1u<<16;c->background.zoom_reactivity_q31=qr(1,4);c->background.warp_reactivity_q31=qr(1,8);c->background.depth_reactivity_q31=qr(1,8);c->background.opacity_q31=INT32_MAX;
 c->core.schema_version=1;c->core.shape=ODM_CORE_SHAPE_CIRCLE;c->core.fit=ODM_CORE_FIT_COVER;c->core.center_x_q31=qr(1,2);c->core.center_y_q31=qr(1,2);c->core.width_q31=qr(1,2);c->core.height_q31=qr(1,2);c->core.corner_radius_q31=qr(1,8);c->core.border_q16=1u<<16;c->core.feather_q16=1u<<16;c->core.scale_reactivity_q31=qr(1,16);c->core.opacity_q31=INT32_MAX;c->core.border_color.r=65535;c->core.border_color.g=65535;c->core.border_color.b=65535;c->core.border_color.a=65535;
 c->field.schema_version=1;c->field.flags=ODM_FIELD_RADIAL_BARS|ODM_FIELD_PARTICLES|ODM_FIELD_ORBIT_RING;c->field.radial_segments=48;c->field.particle_count=8;c->field.ring_gap_q16=2u<<16;c->field.bar_min_q16=1u<<16;c->field.bar_max_q16=4u<<16;c->field.bar_width_q16=1u<<16;c->field.particle_radius_q16=1u<<16;c->field.field_opacity_q31=qr(3,4);c->field.primary_color.r=65535;c->field.primary_color.g=65535;c->field.primary_color.b=65535;c->field.primary_color.a=65535;c->field.secondary_color.r=32767;c->field.secondary_color.g=32767;c->field.secondary_color.b=32767;c->field.secondary_color.a=65535;c->field.seed=UINT64_C(0x0d130d130d130d13);
 c->hud.schema_version=1;c->hud.flags=ODM_HUD_PROGRESS_BAR|ODM_HUD_TIME_CODE|ODM_HUD_TITLE|ODM_HUD_ARTIST;c->hud.margin_q16=2u<<16;c->hud.progress_height_q16=1u<<16;c->hud.progress_width_q31=qr(3,4);c->hud.text_scale_q16=1u<<16;c->hud.line_gap_q16=1u<<16;c->hud.opacity_q31=INT32_MAX;c->hud.foreground_color.r=65535;c->hud.foreground_color.g=65535;c->hud.foreground_color.b=65535;c->hud.foreground_color.a=65535;c->hud.background_color.a=65535;memcpy(c->hud.title,"QUIET, NOT EMPTY",17);memcpy(c->hud.artist,"AFTERIMAGE",11);
}
static void states(odm_composition_frame_state*c,odm_director_frame_state*d){uint32_t i;memset(c,0,sizeof(*c));memset(d,0,sizeof(*d));c->schema_version=1;c->mode=2;c->tick_index=50;c->center_sample=24000;c->phase=UINT32_C(0x12345678);c->ring_phase=UINT32_C(0x34567890);c->core_breath_q31=qr(1,3);c->grid_q31=qr(1,2);c->particles_q31=qr(1,2);c->radial_gain_q31=qr(3,4);c->fracture_q31=qr(1,5);for(i=0;i<48;i++)c->radial_q31[i]=(uint32_t)(((uint64_t)INT32_MAX*(i+1u))/48u);d->schema_version=3;d->tick_index=50;d->layout=1;d->depth_q31=qr(2,5);d->edge_activity_q31=qr(1,4);d->orbit_phase=UINT32_C(0x10203040);}
int main(void){odm_layered_config c;odm_composition_frame_state s;odm_director_frame_state d;odm_layered_frame_plan p;odm_export_plan e;uint8_t cb[1024],pb[1536];uint64_t r=0;odm_sha256_digest h;uint32_t i;cfg(&c);states(&s,&d);
 if(odm_layered_config_bytes(&c,cb,sizeof(cb),&r)!=ODM_STATUS_OK||r!=1024){return 2;}
 printf("C,");hx(cb,sizeof(cb));putchar('\n');
 if(odm_layered_config_sha256(&c,&h)){return 3;}
 printf("H,");hx(h.bytes,32);putchar('\n');
 if(odm_layered_policy_bytes(pb,sizeof(pb),&r)!=ODM_STATUS_OK||r!=1536){return 4;}
 printf("L,");hx(pb,sizeof(pb));putchar('\n');
 if(odm_layered_policy_current_sha256(&h)){return 5;}
 printf("Q,");hx(h.bytes,32);putchar('\n');
 if(odm_layered_resolve_frame_plan(&c,&s,&d,24000,48000,&p)){return 6;}
 printf("P,%u,%u,%u,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%llu,%lld,%lld,%u,%u,%u,%u,%u,%u,%u,",p.schema_version,p.width,p.height,p.fps,p.center_x_q16,p.center_y_q16,p.core_half_w_q16,p.core_half_h_q16,p.core_radius_q16,p.core_corner_q16,p.ring_inner_radius_q16,p.grid_spacing_q16,p.grid_line_q16,p.grid_feather_q16,p.grid_offset_x_q16,p.grid_offset_y_q16,p.grid_warp_q16,p.grid_depth_q16,p.safe_left_q16,p.safe_top_q16,(unsigned long long)p.tick_index,(long long)p.sample,(long long)p.duration_samples,p.core_opacity_q31,p.field_opacity_q31,p.progress_q31,p.particle_count,p.ring_phase,p.layout,p.flags);printf("%d,%d,",p.safe_right_q16,p.safe_bottom_q16);for(i=0;i<48;i++)printf("%u%s",p.radial_q31[i],i==47?",":":");hx(p.config_sha256.bytes,32);putchar('\n');
 if(odm_export_plan_build(&c,48001,48001,ODM_LAYERED_PIXEL_RGBA16LE_LINEAR_PREMUL,&e)){return 7;}
 printf("E,%u,%u,%u,%u,%d,%u,%u,%lld,%lld,%llu,%lld,%llu,%llu,%llu,%llu,",e.schema_version,e.output_pixel_format,e.width,e.height,e.fps,e.audio_sample_rate,e.audio_channels,(long long)e.project_end_sample,(long long)e.samples_per_frame,(unsigned long long)e.frame_count,(long long)e.output_end_sample,(unsigned long long)e.audio_source_frames,(unsigned long long)e.audio_pad_frames,(unsigned long long)e.video_bytes_per_frame,(unsigned long long)e.video_total_bytes);hx(e.layered_policy_sha256.bytes,32);putchar(',');hx(e.config_sha256.bytes,32);putchar('\n');
 {
   static const int64_t xs[6]={0,1,INT64_MAX/2,INT64_MAX-2,INT64_MAX-1,INT64_MAX};
   size_t xi;
   printf("X");
   for(xi=0;xi<6u;++xi){
     if(odm_layered_resolve_frame_plan(&c,&s,&d,xs[xi],INT64_MAX,&p)){return 8;}
     printf(",%lld:%u",(long long)xs[xi],p.progress_q31);
   }
   putchar('\n');
 }
 return 0;}
'''

def qr(n,d): return (Q*n+d//2)//d
def u16(b,v): b.extend(struct.pack('<H',v))
def u32(b,v): b.extend(struct.pack('<I',v & 0xffffffff))
def i32(b,v): b.extend(struct.pack('<i',v))
def u64(b,v): b.extend(struct.pack('<Q',v))
def rgba(b,c):
    for v in c:u16(b,v)

def config_bytes():
    b=bytearray(b'ODMCFG14')
    # top/canvas
    for v in (1,0, 1,2,32,32):u32(b,v)
    i32(b,30);u32(b,0)
    safe=qr(1,20)
    for v in (safe,safe,safe,safe):u32(b,v)
    # background
    u32(b,1);u32(b,2);rgba(b,(0,0,0,65535));rgba(b,(65535,65535,65535,65535))
    for v in (8<<16,1<<16,1<<16,qr(1,4),qr(1,8),qr(1,8),Q):u32(b,v)
    # core
    for v in (1,1,1,qr(1,2),qr(1,2),qr(1,2),qr(1,2),qr(1,8),1<<16,1<<16,qr(1,16),Q):u32(b,v)
    rgba(b,(65535,65535,65535,65535))
    # field
    for v in (1,7,48,8,2<<16,1<<16,4<<16,1<<16,1<<16,qr(3,4)):u32(b,v)
    rgba(b,(65535,65535,65535,65535));rgba(b,(32767,32767,32767,65535))
    # Las particulas llevan su propio color; la sonda no lo fija, asi que es cero.
    rgba(b,(0,0,0,0))
    u64(b,0x0d130d130d130d13)
    # HUD
    for v in (1,15,2<<16,1<<16,qr(3,4),1<<16,1<<16,Q):u32(b,v)
    rgba(b,(65535,65535,65535,65535));rgba(b,(0,0,0,65535))
    title=b'QUIET, NOT EMPTY\0'; artist=b'AFTERIMAGE\0'
    b.extend(title+b'\0'*(96-len(title)));b.extend(artist+b'\0'*(96-len(artist)))
    # anclaje, estilo de barra y modo de tiempo; luego los cuatro colores propios
    # del HUD (titulo, autoria, barra y pista) y el reservado.
    for _ in range(3):u32(b,0)
    for _ in range(4):rgba(b,(0,0,0,0))
    u32(b,0)
    if len(b)>CFG_BYTES: raise AssertionError(len(b))
    b.extend(b'\0'*(CFG_BYTES-len(b)))
    return bytes(b)

def policy_bytes():
    b=bytearray(b'ODMLAYR3')
    # v14: paridad de las dos rutas de fondo, radio radial = media diagonal.
    # v15: difuminado ordenado declarado en la configuracion.
    vals0=(15,1,1,3,6,6,1,2,3,3,48,96,1,8,1,1,1)
    for v in vals0:u32(b,v)
    u64(b,0x51a7c0de9e3779b9)
    for v in (73,536870912,4,2,1,512,1,2,65536,
              1,1,1,1,1,1,1,1,1,1,1,1,1,
              # v12: estilos de fondo (5 con el campo de profundidad), luego el
              # contrato del difuminado ordenado: matriz Bayer 8x8 determinista,
              # 64 celdas, y perfil de profundidad (1-(d/r)^2)^2.
              9,1,64,255,1,2,
              1,1,1,
              6,3,3,1,1,
              1,26,6,6,1,1,1,16,
              # 96->48 pair reduction, tip start, tip end, then the v11 tip
              # opacity curve id: 2 = QUADRATIC in same-lane attack. Policy v10
              # encoded 1 ("exact same-lane attack") while the tree already
              # squared it. See docs/RADIAL_MORPHOLOGY_V2.md section 4.3.
              1,1,1,2,
              1,1,1,1,1,32,1,1,1,
              # v11 dual-domain optical authority for the held body
              # (docs/RADIAL_MORPHOLOGY_V2.md section 4.1): opacity model id,
              # 0.15 authority floor, 0.65 release tail weight, activity is the
              # max of the two domains, and segment lengths stay linear.
              1,322122547,1395864371,1,1):u32(b,v)
    if len(b)>POLICY_BYTES:raise AssertionError(len(b))
    b.extend(b'\0'*(POLICY_BYTES-len(b)))
    return bytes(b)

def mulq(a,b): return min(Q,(a*b+Q//2)//Q)
def ratio_px(dim,q): return min(2147483647,(dim*65536*q+Q//2)//Q)
def mul_q16_q31(v,f):
    p=v*f
    p = p+Q//2 if p>=0 else p-Q//2
    # C division truncates toward zero
    p = p//Q if p>=0 else -((-p)//Q)
    return max(-2147483648,min(2147483647,p))

def expected_plan(cfg_sha:bytes):
    base_w=ratio_px(32,qr(1,2));base_h=base_w
    react=mulq(qr(1,16),qr(1,3));add=mul_q16_q31(base_w,react)//2
    half=(base_w+add)//2
    corner=mul_q16_q31(half,qr(1,8));ring=half+(2<<16)
    spacing=8<<16; react=mulq(qr(1,4),qr(1,2));spacing_add=mul_q16_q31(spacing,react)//2; spacing+=spacing_add
    phase=0x12345678; offx=(phase*spacing)>>32; offy=(((phase+0x40000000)&0xffffffff)*spacing)>>32
    react=mulq(qr(1,8),max(qr(1,5),qr(1,4)));warp=mul_q16_q31(spacing,react)
    react=mulq(qr(1,8),qr(2,5));depth=mul_q16_q31(spacing,react)
    safe=ratio_px(32,qr(1,20));safe_r=(32<<16)-safe
    field_op=mulq(qr(3,4),max(qr(3,4),qr(1,2)))
    prog=(24000*Q+48000//2)//48000
    pg=Q//4 + mulq(qr(1,2),Q*3//4)
    pc=(8*pg+Q//2)//Q
    radial=[(Q*(i+1))//48 for i in range(48)]
    return [1,32,32,30,ratio_px(32,qr(1,2)),ratio_px(32,qr(1,2)),half,half,half,corner,ring,spacing,1<<16,1<<16,offx,offy,warp,depth,safe,safe,50,24000,48000,Q,field_op,prog,pc,(0x34567890+0x10203040)&0xffffffff,1,0,safe_r,safe_r],radial,cfg_sha

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--root',required=True);ap.add_argument('--cc',default='gcc');ap.add_argument('--library',required=True);a=ap.parse_args()
    root=pathlib.Path(a.root).resolve();lib=pathlib.Path(a.library).resolve()
    with tempfile.TemporaryDirectory(prefix='odm-layered-oracle-') as td:
        td=pathlib.Path(td);src=td/'p.c';exe=td/'p';src.write_text(PROBE+'\n')
        cmd=[a.cc,'-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-I',str(root/'include'),str(src),str(lib),'-pthread','-lpng16','-lssl','-lcrypto','-o',str(exe)]
        r=subprocess.run(cmd,cwd=root,capture_output=True,text=True)
        if r.returncode:raise SystemExit('layered oracle probe compile failed\n'+r.stdout+r.stderr)
        x=subprocess.run([str(exe)],cwd=root,capture_output=True,text=True)
        if x.returncode:raise SystemExit(f'layered oracle probe failed rc={x.returncode}\n{x.stdout}\n{x.stderr}')
    rows={ln.split(',',1)[0]:ln.split(',',1)[1] for ln in x.stdout.splitlines() if ',' in ln}
    expc=config_bytes(); expl=policy_bytes()
    gotc=bytes.fromhex(rows['C']);gotl=bytes.fromhex(rows['L'])
    if gotc!=expc:
        j=next(i for i,(u,v) in enumerate(zip(gotc,expc)) if u!=v);raise SystemExit(f'config byte mismatch offset={j} got={gotc[j]} exp={expc[j]}')
    if gotl!=expl:
        j=next(i for i,(u,v) in enumerate(zip(gotl,expl)) if u!=v);raise SystemExit(f'policy byte mismatch offset={j} got={gotl[j]} exp={expl[j]}')
    csha=hashlib.sha256(expc).digest();lsha=hashlib.sha256(expl).digest()
    if bytes.fromhex(rows['H'])!=csha:raise SystemExit('config hash mismatch')
    if bytes.fromhex(rows['Q'])!=lsha:raise SystemExit('policy hash mismatch')
    p=rows['P'].split(','); nums=list(map(int,p[:32]));rad=list(map(int,p[32].split(':')));ph=bytes.fromhex(p[33])
    en,er,eh=expected_plan(csha)
    if nums!=en: raise SystemExit(f'frame plan scalar mismatch\n got={nums}\n exp={en}')
    if rad!=er: raise SystemExit('frame plan radial mismatch')
    if ph!=eh: raise SystemExit('frame plan config hash mismatch')
    e=rows['E'].split(',');enums=list(map(int,e[:15]));epol=bytes.fromhex(e[15]);ecfg=bytes.fromhex(e[16])
    # 48001 at 30 fps => spf=1600, ceil =>31, output 49600
    exp_e=[1,1,32,32,30,48000,2,48001,1600,31,49600,48001,1599,32*32*8,32*32*8*31]
    if enums!=exp_e:raise SystemExit(f'export plan mismatch got={enums} exp={exp_e}')
    if epol!=lsha or ecfg!=csha:raise SystemExit('export identities mismatch')
    # Extreme progress values are reconstructed with Python arbitrary-precision
    # integers. This specifically proves the C path never relies on a wrapped
    # uint64_t product near INT64_MAX.
    xparts=rows['X'].split(',')
    gotx=[]
    for item in xparts:
        sv,pv=item.split(':',1); gotx.append((int(sv),int(pv)))
    d=(1<<63)-1
    samples=[0,1,d//2,d-2,d-1,d]
    expx=[]
    for sv in samples:
        if sv<=0: pv=0
        elif sv>=d: pv=Q
        else: pv=(sv*Q+d//2)//d
        expx.append((sv,pv))
    if gotx!=expx: raise SystemExit(f'extreme progress mismatch got={gotx} exp={expx}')
    print('LAYERED ORACLE: PASS')
    print(f'  config: {CFG_BYTES} bytes, sha256={csha.hex()}')
    print(f'  policy: {POLICY_BYTES} bytes, sha256={lsha.hex()}')
    print('  frame plan: independent fixed-point reconstruction exact')
    print('  progress: arbitrary-precision oracle exact through INT64_MAX')
    print('  export timeline: 31 frames / 49,600 samples exact')
    return 0
if __name__=='__main__':raise SystemExit(main())
