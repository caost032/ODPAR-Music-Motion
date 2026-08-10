#!/usr/bin/env python3
"""Independent ODPAR 0.14 Style Profile oracle.

The C engine is only the observed system. Expected 512-byte style records and
style->Layered Config materialization are reconstructed from literal public
contract values in Python.
"""
from __future__ import annotations
import argparse, hashlib, pathlib, struct, subprocess, tempfile

Q=2147483647
STYLE_BYTES=512
CFG_BYTES=1024
ONE=65536

PROBE=r'''#include "odm_compositor.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static void hx(const uint8_t*p,size_t n){size_t i;for(i=0;i<n;++i)printf("%02x",p[i]);}
int main(void){
  uint32_t id; uint8_t b[ODM_LAYERED_STYLE_BYTES],cb[ODM_LAYERED_CONFIG_BYTES]; uint64_t r=0;
  odm_layered_style_profile s; odm_layered_config base,out; odm_sha256_digest h;
  for(id=ODM_LAYERED_STYLE_PRESET_MONO_PRECISE;id<=ODM_LAYERED_STYLE_PRESET_MINIMAL_EDITORIAL;++id){
    if(odm_layered_style_init(&s,id)!=ODM_STATUS_OK)return 2;
    if(odm_layered_style_bytes(&s,b,sizeof(b),&r)!=ODM_STATUS_OK||r!=ODM_LAYERED_STYLE_BYTES)return 3;
    if(odm_layered_style_sha256(&s,&h)!=ODM_STATUS_OK)return 4;
    printf("S,%u,",id);hx(b,sizeof(b));putchar(',');hx(h.bytes,32);putchar('\n');
  }
  if(odm_layered_config_init_default(&base,ODM_CANVAS_ASPECT_SQUARE_1_1,1080,1080,30)!=ODM_STATUS_OK)return 5;
  if(odm_layered_config_set_metadata(&base,"Quiet, Not Empty","AFTERIMAGE")!=ODM_STATUS_OK)return 6;
  if(odm_layered_style_init(&s,ODM_LAYERED_STYLE_PRESET_VOID_CINEMATIC)!=ODM_STATUS_OK)return 7;
  if(odm_layered_style_apply(&base,&s,&out)!=ODM_STATUS_OK)return 8;
  if(odm_layered_config_bytes(&out,cb,sizeof(cb),&r)!=ODM_STATUS_OK||r!=ODM_LAYERED_CONFIG_BYTES)return 9;
  if(odm_layered_config_sha256(&out,&h)!=ODM_STATUS_OK)return 10;
  printf("C,");hx(cb,sizeof(cb));putchar(',');hx(h.bytes,32);putchar('\n');
  return 0;
}
'''

def u16(b,v): b.extend(struct.pack('<H',v))
def u32(b,v): b.extend(struct.pack('<I',v & 0xffffffff))
def i32(b,v): b.extend(struct.pack('<i',v))
def u64(b,v): b.extend(struct.pack('<Q',v))
def reaction_route(a,b=0,combine=0): return a | (b<<8) | (combine<<16)
def rgba(b,c):
    for v in c:u16(b,v)
def qr(n,d): return (Q*n+d//2)//d
def scaled_q16(min_dim,divisor,min_px,max_px):
    lo=min_px<<16; hi=max_px<<16
    v=(min_dim<<16)//divisor if divisor else lo
    return max(lo,min(hi,v))
def scale_u32(v,s,maxv=0xffffffff):
    p=(v*s+32768)>>16
    return min(maxv,p)
def scale_q31(v,s): return scale_u32(v,s,Q)

def preset_values(pid:int):
    d=dict(
        schema=1,preset=pid,mask=15,flags=0,
        bg_solid=(0,0,0,65535),bg_grid=(65535,65535,65535,16384),
        core=(65535,65535,65535,65535),field1=(65535,65535,65535,65535),
        field2=(32768,32768,32768,65535),hudfg=(65535,65535,65535,65535),
        hudbg=(0,0,0,24576),bgop=Q,fieldop=Q,hudop=Q,
        scales=[ONE]*20,progress=2,anchor=4,time=0)
    if pid==1:
        d['bg_grid']=(65535,65535,65535,24576); d['fieldop']=1610612735
    elif pid==2:
        d['bg_grid']=(65535,65535,65535,9216); d['field2']=(16384,16384,16384,65535)
        d['fieldop']=1342177279; d['hudop']=1879048191
        d['scales']=[81920,49152,65536,65536,49152,81920,58982,49152,65536,65536,
                     81920,49152,81920,49152,32768,49152,65536,49152,65536,65536]
    elif pid==3:
        d['bg_grid']=(65535,65535,65535,20480); d['fieldop']=1503238553
        d['scales']=[73728,65536,65536,81920,98304,98304,65536,65536,65536,65536,
                     73728,65536,65536,65536,49152,65536,65536,65536,65536,65536]
    elif pid==4:
        d['bg_grid']=(65535,65535,65535,6144); d['fieldop']=1073741823; d['hudop']=1610612735
        d['scales']=[98304,32768,49152,65536,32768,65536,65536,32768,65536,65536,
                     98304,32768,65536,32768,24576,32768,65536,32768,65536,65536]
    else: raise ValueError(pid)
    return d

def style_bytes(pid:int):
    d=preset_values(pid); b=bytearray(b'ODMSTY14')
    for v in (d['schema'],d['preset'],d['mask'],d['flags']):u32(b,v)
    for c in (d['bg_solid'],d['bg_grid'],d['core'],d['field1'],d['field2'],d['hudfg'],d['hudbg']):rgba(b,c)
    for v in (d['bgop'],d['fieldop'],d['hudop']):u32(b,v)
    for v in d['scales']:u32(b,v)
    for v in (d['progress'],d['anchor'],d['time']):u32(b,v)
    if len(b)>STYLE_BYTES:raise AssertionError(len(b))
    b.extend(b'\0'*(STYLE_BYTES-len(b)))
    return bytes(b)

def default1080_void_config_bytes():
    # Reconstruct odm_layered_config_init_default(1:1,1080,30) followed by
    # metadata setter and VOID_CINEMATIC style materialization.
    min_dim=1080; d=preset_values(2); sc=d['scales']
    bg_spacing=scale_u32(scaled_q16(min_dim,16,8,1024),sc[0])
    bg_line=scale_u32(scaled_q16(min_dim,720,1,8),sc[1])
    bg_feather=scale_u32(1<<16,sc[2])
    bg_zoom=scale_q31(qr(1,4),sc[3]); bg_warp=scale_q31(qr(1,8),sc[4]); bg_depth=scale_q31(qr(1,8),sc[5])
    core_wh=scale_q31(qr(9,20),sc[6]); core_border=scale_u32(scaled_q16(min_dim,720,1,8),sc[7])
    core_feather=scale_u32(1<<16,sc[8]); core_react=scale_q31(qr(1,16),sc[9])
    particles=min(128,max(16,min_dim//16)); particles=scale_u32(particles,sc[14],512)
    ring=scale_u32(scaled_q16(min_dim,96,2,128),sc[10]); bmin=scale_u32(scaled_q16(min_dim,240,1,64),sc[11])
    bmax=scale_u32(scaled_q16(min_dim,40,4,512),sc[12]); bw=scale_u32(scaled_q16(min_dim,720,1,16),sc[13])
    prad=scale_u32(scaled_q16(min_dim,720,1,16),sc[15])
    margin=scale_u32(scaled_q16(min_dim,25,8,512),sc[16]); ph=scale_u32(scaled_q16(min_dim,360,1,32),sc[17])
    textpx=max(1,min(32,min_dim//540)); text=scale_u32(textpx<<16,sc[18]); gap=scale_u32(1<<16,sc[19])
    b=bytearray(b'ODMCFG14')
    for v in (1,0,1,2,1080,1080):u32(b,v)
    i32(b,30);u32(b,0)
    safe=qr(1,20)
    for v in (safe,safe,safe,safe):u32(b,v)
    u32(b,1);u32(b,3);rgba(b,d['bg_solid']);rgba(b,d['bg_grid'])
    for v in (bg_spacing,bg_line,bg_feather,bg_zoom,bg_warp,bg_depth,Q):u32(b,v)
    for v in (1,1,1,qr(1,2),qr(1,2),core_wh,core_wh,qr(1,8),core_border,core_feather,core_react,Q):u32(b,v)
    rgba(b,d['core'])
    # Ganancia emisiva del nucleo: la configuracion por omision no la pide y el
    # perfil de estilo tampoco la toca, asi que vale cero.
    u32(b,0)
    for v in (1,7,48,particles,ring,bmin,bmax,bw,prad,d['fieldop']):u32(b,v)
    rgba(b,d['field1']);rgba(b,d['field2'])
    # Las particulas llevan color propio. Por omision arranca igual que el
    # acento de las agujas, de modo que separar la categoria no cambia lo que
    # ya se dibujaba; el perfil de estilo lo materializa con el mismo valor.
    rgba(b,d['field1'])
    # Forma de barra y profundidad de particula: la configuracion por omision
    # es trazo simple y campo plano.
    u32(b,0);u32(b,0)
    # Gobierno del aire simulado: dibujo, naturaleza, caudal, rozamiento,
    # impulso y aleteo. Todo a cero -- el aire es una decision del diseno, no
    # algo que la configuracion por omision encienda por su cuenta.
    for _ in range(6):u32(b,0)
    u64(b,0x0d130d130d130d13)
    for v in (1,15,margin,ph,qr(3,4),text,gap,d['hudop']):u32(b,v)
    rgba(b,d['hudfg']);rgba(b,d['hudbg'])
    title=b'Quiet, Not Empty\0'; artist=b'AFTERIMAGE\0'
    b.extend(title+b'\0'*(96-len(title)));b.extend(artist+b'\0'*(96-len(artist)))
    for v in (d['anchor'],d['progress'],d['time']):u32(b,v)
    # Colores propios del HUD: titulo, autoria, barra y pista. Titulo, autoria
    # y barra parten del color de primer plano; la pista, del fondo del HUD tal
    # y como lo deja la configuracion por omision -- (0,0,0,32768).
    #
    # La pista NO sigue al perfil de estilo. El perfil tiene una ranura para el
    # fondo del HUD, y bajo separacion de categorias esa ranura gobierna el
    # fondo del HUD, no la pista de la barra: son controles distintos. Un estilo
    # que quiera mover la pista necesita su propia ranura, no heredarla.
    for _ in range(3):rgba(b,d['hudfg'])
    rgba(b,(0,0,0,32768))
    u32(b,0)
    # Explicit canonical Reaction Matrix v1 from odm_layered_config_init_default.
    for v in (1,0,
              reaction_route(2),
              reaction_route(5,19,1),
              reaction_route(18),
              reaction_route(1),
              reaction_route(4,3,1),
              reaction_route(3)):
        u32(b,v)
    if len(b)>CFG_BYTES:raise AssertionError(len(b))
    b.extend(b'\0'*(CFG_BYTES-len(b)))
    return bytes(b)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--root',required=True);ap.add_argument('--cc',default='gcc');ap.add_argument('--library',required=True);a=ap.parse_args()
    root=pathlib.Path(a.root).resolve(); lib=pathlib.Path(a.library).resolve()
    with tempfile.TemporaryDirectory(prefix='odm-style-oracle-') as td:
        td=pathlib.Path(td); src=td/'p.c'; exe=td/'p'; src.write_text(PROBE+'\n')
        cmd=[a.cc,'-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-I',str(root/'include'),str(src),str(lib),'-pthread','-lpng16','-lssl','-lcrypto','-o',str(exe)]
        r=subprocess.run(cmd,cwd=root,capture_output=True,text=True)
        if r.returncode: raise SystemExit('style oracle compile failed\n'+r.stdout+r.stderr)
        x=subprocess.run([str(exe)],cwd=root,capture_output=True,text=True)
        if x.returncode: raise SystemExit(f'style oracle probe failed rc={x.returncode}\n{x.stdout}\n{x.stderr}')
    styles={}; crow=None
    for ln in x.stdout.splitlines():
        p=ln.split(',')
        if p[0]=='S':styles[int(p[1])]=(bytes.fromhex(p[2]),bytes.fromhex(p[3]))
        elif p[0]=='C':crow=(bytes.fromhex(p[1]),bytes.fromhex(p[2]))
    if set(styles)!={1,2,3,4} or crow is None: raise SystemExit('missing oracle rows')
    for pid,(got,gh) in styles.items():
        exp=style_bytes(pid); eh=hashlib.sha256(exp).digest()
        if got!=exp:
            j=next(i for i,(u,v) in enumerate(zip(got,exp)) if u!=v);raise SystemExit(f'style {pid} byte mismatch offset={j} got={got[j]} exp={exp[j]}')
        if gh!=eh: raise SystemExit(f'style {pid} hash mismatch')
    expc=default1080_void_config_bytes(); ech=hashlib.sha256(expc).digest()
    if crow[0]!=expc:
        j=next(i for i,(u,v) in enumerate(zip(crow[0],expc)) if u!=v);raise SystemExit(f'styled config mismatch offset={j} got={crow[0][j]} exp={expc[j]}')
    if crow[1]!=ech: raise SystemExit('styled config hash mismatch')
    print('STYLE ORACLE: PASS')
    for pid in range(1,5): print(f'  preset {pid}: 512 bytes sha256={hashlib.sha256(style_bytes(pid)).hexdigest()}')
    print(f'  VOID_CINEMATIC -> 1080x1080 config sha256={ech.hex()}')
    print('  style application: independent Python materialization byte-exact')
    return 0
if __name__=='__main__': raise SystemExit(main())
