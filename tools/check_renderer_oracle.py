#!/usr/bin/env python3
"""Independent Gate-6 CPU reference-renderer oracle.

The C probe builds one canonical Score/Render-IR and renders six tiny frames.
Python independently rebuilds the asset identity, fixed-point color pipeline,
transform/bilinear sampler, per-scene compositing, crossfade, frame hashes and
frame-root. Expected pixels/hashes are never obtained from the C renderer.
"""
from __future__ import annotations

import argparse
from decimal import Decimal, getcontext, ROUND_HALF_UP
import hashlib
import pathlib
import struct
import subprocess
import tempfile

I32_MAX = (1 << 31) - 1
I32_MIN = -(1 << 31)
Q32 = 1 << 32
Q27 = 1 << 27
MASK64 = (1 << 64) - 1

IMAGE = 1
VIDEO = 2
PIX_RGBA8 = 1
PRIM_BT709 = 1
TRANSFER_LINEAR = 1
TRANSFER_SRGB = 2
ALPHA_STRAIGHT = 1
OUT_RGBA16 = 1
FPS = 30
WIDTH = 8
HEIGHT = 8
FRAME_COUNT = 6
OUTPUT_END = 9600

RING = bytes([
    0,0,0,0, 255,220,120,255, 255,220,120,255, 0,0,0,0,
    255,220,120,255, 0,0,0,0, 0,0,0,0, 255,220,120,255,
    255,220,120,255, 0,0,0,0, 0,0,0,0, 255,220,120,255,
    0,0,0,0, 255,220,120,255, 255,220,120,255, 0,0,0,0,
])
VRED = bytes([255,0,0,255] * 4)
VBLUE = bytes([0,0,255,255] * 4)
RING_SOURCE = hashlib.sha256(b"ring-source").digest()
VIDEO_SOURCE = hashlib.sha256(b"video-source").digest()


def le32(v: int) -> bytes: return struct.pack('<I', v & 0xffffffff)
def le64(v: int) -> bytes: return struct.pack('<Q', v & MASK64)
def lei64(v: int) -> bytes: return struct.pack('<q', v)


def div_trunc(n: int, d: int) -> int:
    assert d != 0
    q = abs(n) // abs(d)
    return -q if (n < 0) != (d < 0) else q


def div_round_away(n: int, d: int) -> int:
    q = div_trunc(n, d)
    r = n - q * d
    rm, dm = abs(r), abs(d)
    if rm >= dm - rm:
        q += -1 if (n < 0) != (d < 0) else 1
    return q


def q1_ratio(n: int, d: int) -> int:
    assert d > 0 and abs(n) <= d
    if abs(n) == d:
        return I32_MIN if n < 0 else I32_MAX
    mag = abs(n); q = 0; rem = mag
    for _ in range(31):
        q <<= 1
        if rem >= d - rem:
            q |= 1; rem -= d - rem
        else:
            rem += rem
    if rem and rem >= d - rem: q += 1
    if n < 0: return I32_MIN if q == (1 << 31) else -q
    return min(q, I32_MAX)


def q1_mul(a: int, b: int) -> int:
    p = a * b; m = abs(p)
    r = (m >> 31) + (1 if (m & 0x7fffffff) >= 0x40000000 else 0)
    if p >= 0: return min(r, I32_MAX)
    return I32_MIN if r >= (1 << 31) else -r


def q32_mul(a: int, b: int) -> int:
    p = a * b; neg = p < 0; m = abs(p)
    q = m >> 32; rem = m & 0xffffffff
    if rem >= 0x80000000: q += 1
    return -q if neg else q


def ratio_unit_q32(n: int, d: int) -> int:
    assert d > 0 and abs(n) <= d
    mag = abs(n); whole, rem = divmod(mag, d); frac = 0
    for _ in range(32):
        rem <<= 1; frac <<= 1
        if rem >= d: rem -= d; frac |= 1
    q = (whole << 32) | frac
    if rem >= d - rem: q += 1
    q = min(q, Q32)
    return -q if n < 0 else q


def q32_frac_to_q31(f: int) -> int:
    p = f * I32_MAX; q = p >> 32
    if (p & 0xffffffff) >= 0x80000000: q += 1
    return min(q, I32_MAX)


def q27_mul_q31(v: int, f: int) -> int:
    return div_round_away(v * f, I32_MAX)


def lerp_den_max(a: int, b: int, t: int) -> int:
    return div_round_away(a * (I32_MAX - t) + b * t, I32_MAX)


def q27_to_u16(v: int) -> int:
    return div_round_away(max(0, min(Q27, v)) * 65535, Q27)


def q31_to_u16(v: int) -> int:
    return div_round_away(max(0, min(I32_MAX, v)) * 65535, I32_MAX)


def srgb_table() -> list[int]:
    getcontext().prec = 80
    out = []
    for i in range(256):
        c = Decimal(i) / Decimal(255)
        if c <= Decimal('0.04045'):
            linear = c / Decimal('12.92')
        else:
            linear = ((c + Decimal('0.055')) / Decimal('1.055')) ** Decimal('2.4')
        out.append(int((linear * Decimal(Q27)).to_integral_value(rounding=ROUND_HALF_UP)))
    return out

SRGB = srgb_table()


def decode_u8(v: int, transfer: int) -> int:
    if transfer == TRANSFER_SRGB: return SRGB[v]
    assert transfer == TRANSFER_LINEAR
    return div_round_away(v * Q27, 255)


def alpha_u8(v: int) -> int:
    return div_round_away(v * I32_MAX, 255)


def texel(pixels: bytes, w: int, h: int, transfer: int, x: int, y: int) -> tuple[int,int,int,int]:
    if x < 0 or y < 0 or x >= w or y >= h: return (0,0,0,0)
    o = (y * w + x) * 4
    a = alpha_u8(pixels[o+3])
    return (q27_mul_q31(decode_u8(pixels[o], transfer), a),
            q27_mul_q31(decode_u8(pixels[o+1], transfer), a),
            q27_mul_q31(decode_u8(pixels[o+2], transfer), a), a)


def pixel_lerp(a, b, t):
    return tuple(lerp_den_max(a[i], b[i], t) for i in range(4))


CORDIC_K_Q31 = 1304065748
QUARTER = 0x40000000
CORDIC_ATAN = [
    536870912,316933406,167458907,85004756,42667331,21354465,10679838,5340245,
    2670163,1335087,667544,333772,166886,83443,41722,20861,10430,5215,2608,
    1304,652,326,163,81,41,20,10,5,3,1,1,
]

def cordic_sin_q31(phase: int) -> int:
    phase &= 0xffffffff
    quadrant=phase>>30; offset=phase&0x3fffffff; sign=-1 if quadrant>=2 else 1
    z=offset if quadrant in (0,2) else QUARTER-offset
    if phase in (0,0x80000000): return 0
    if phase==0x40000000: return I32_MAX
    if phase==0xc0000000: return -I32_MAX
    x,y=CORDIC_K_Q31,0
    for i,a in enumerate(CORDIC_ATAN):
        xs=div_trunc(x,1<<i);ys=div_trunc(y,1<<i)
        if z>=0: x,y,z=x-ys,y+xs,z-a
        else: x,y,z=x+ys,y-xs,z+a
    y*=sign
    return max(-I32_MAX,min(I32_MAX,y))

def phase_sincos_q32(phase: int) -> tuple[int,int]:
    phase &= 0xffffffff
    if phase==0:return 0,Q32
    if phase==0x40000000:return Q32,0
    if phase==0x80000000:return 0,-Q32
    if phase==0xc0000000:return -Q32,0
    return cordic_sin_q31(phase)*2,cordic_sin_q31((phase+QUARTER)&0xffffffff)*2

def sample_surface(pixels: bytes, sw: int, sh: int, transfer: int,
                   out_x: int, out_y: int, scale_x: int, scale_y: int,
                   opacity: int, phase: int, x_q32: int = 0, y_q32: int = 0) -> tuple[int,int,int,int]:
    if scale_x <= 0 or scale_y <= 0 or opacity <= 0: return (0,0,0,0)
    nx = (((out_x * 2 + 1) * Q32) // WIDTH) - Q32
    ny = (((out_y * 2 + 1) * Q32) // HEIGHT) - Q32
    dx=nx-x_q32;dy=ny-y_q32
    sinv,cosv=phase_sincos_q32(phase)
    rx = q32_mul(dx, cosv) + q32_mul(dy, sinv)
    ry = q32_mul(dy, cosv) - q32_mul(dx, sinv)
    if abs(rx) > scale_x or abs(ry) > scale_y: return (0,0,0,0)
    lx = ratio_unit_q32(rx, scale_x); ly = ratio_unit_q32(ry, scale_y)
    tx = ((lx + Q32) * sw) // 2 - Q32 // 2
    ty = ((ly + Q32) * sh) // 2 - Q32 // 2
    ix = div_trunc(tx, Q32); remx = tx - ix * Q32
    iy = div_trunc(ty, Q32); remy = ty - iy * Q32
    if remx < 0: ix -= 1; remx += Q32
    if remy < 0: iy -= 1; remy += Q32
    wx = q32_frac_to_q31(remx); wy = q32_frac_to_q31(remy)
    p00=texel(pixels,sw,sh,transfer,ix,iy); p10=texel(pixels,sw,sh,transfer,ix+1,iy)
    p01=texel(pixels,sw,sh,transfer,ix,iy+1); p11=texel(pixels,sw,sh,transfer,ix+1,iy+1)
    top=pixel_lerp(p00,p10,wx); bottom=pixel_lerp(p01,p11,wx); sample=pixel_lerp(top,bottom,wy)
    return (q27_mul_q31(sample[0],opacity), q27_mul_q31(sample[1],opacity),
            q27_mul_q31(sample[2],opacity), q1_mul(sample[3],opacity))


def blend_over(dst, src):
    inv = I32_MAX - src[3]
    out = [src[i] + q27_mul_q31(dst[i], inv) for i in range(3)]
    out = [min(Q27, x) for x in out]
    a = src[3] + (dst[3] * inv + I32_MAX // 2) // I32_MAX
    return (out[0],out[1],out[2],min(I32_MAX,a))


def video_for_local(local: int):
    if 0 <= local < 2400: return VRED
    if 2400 <= local < 4800: return VBLUE
    raise AssertionError(f'video local sample out of range: {local}')


def scene1_pixel(x: int, y: int):
    p=(0,0,0,I32_MAX)  # Black Void over transparent clears to opaque black.
    primary=sample_surface(RING,4,4,TRANSFER_SRGB,x,y,Q32,Q32,I32_MAX,0)
    p=blend_over(p,primary)
    subject=sample_surface(RING,4,4,TRANSFER_SRGB,x,y,5*Q32//8,3*Q32//8,q1_ratio(3,5),0x20000000)
    return blend_over(p,subject)


def scene2_pixel(x: int, y: int, sample: int):
    p=(0,0,0,I32_MAX)
    video=sample_surface(video_for_local(sample-4800),2,2,TRANSFER_LINEAR,x,y,Q32,Q32,I32_MAX,0)
    return blend_over(p,video)


def expected_frame(fi: int) -> tuple[int,bytes]:
    sample=fi*1600
    if sample < 4800: a,b,t=1,0,0
    elif sample < 8000: a,b,t=1,2,q1_ratio(sample-4800,3200)
    else: a,b,t=2,0,0
    out=bytearray()
    for y in range(HEIGHT):
        for x in range(WIDTH):
            pa=scene1_pixel(x,y) if a==1 else scene2_pixel(x,y,sample)
            if b:
                pb=scene2_pixel(x,y,sample)
                p=tuple(lerp_den_max(pa[i],pb[i],t) for i in range(4))
            else: p=pa
            out += struct.pack('<HHHH',q27_to_u16(p[0]),q27_to_u16(p[1]),q27_to_u16(p[2]),q31_to_u16(p[3]))
    assert len(out)==512
    return sample,bytes(out)


def asset_hash() -> bytes:
    h=hashlib.sha256();h.update(b'ODPAR_RENDER_ASSETS_V1\0');h.update(le32(2))
    # image
    h.update(le32(1)+le32(IMAGE)+le32(1)+RING_SOURCE)
    h.update(le32(4)+le32(4)+le32(PIX_RGBA8)+le32(PRIM_BT709)+le32(TRANSFER_SRGB)+le32(ALPHA_STRAIGHT))
    h.update(lei64(0)+lei64(0)+le64(len(RING))+hashlib.sha256(RING).digest())
    # video
    h.update(le32(2)+le32(VIDEO)+le32(2)+VIDEO_SOURCE)
    for start,end,pixels in ((0,2400,VRED),(2400,4800,VBLUE)):
        h.update(le32(2)+le32(2)+le32(PIX_RGBA8)+le32(PRIM_BT709)+le32(TRANSFER_LINEAR)+le32(ALPHA_STRAIGHT))
        h.update(lei64(start)+lei64(end)+le64(len(pixels))+hashlib.sha256(pixels).digest())
    return h.digest()


def frame_hash(irsha: bytes, asha: bytes, fi: int, sample: int, pixels: bytes) -> bytes:
    h=hashlib.sha256();h.update(b'ODPAR_REF_FRAME_V1\0');h.update(irsha);h.update(asha)
    h.update(le32(1)+le32(0)+le32(FPS))
    h.update(lei64(fi)+lei64(sample)+le32(WIDTH)+le32(HEIGHT)+le32(OUT_RGBA16)+le64(len(pixels))+pixels)
    return h.digest()


def root_hash(irsha: bytes, asha: bytes, frame_hashes: list[bytes]) -> bytes:
    h=hashlib.sha256();h.update(b'ODPAR_REF_FRAME_ROOT_V1\0');h.update(irsha);h.update(asha)
    h.update(le32(1)+le32(0)+le32(OUT_RGBA16)+le32(WIDTH)+le32(HEIGHT)+le32(FPS)+le64(FRAME_COUNT)+le64(OUTPUT_END))
    for i,fh in enumerate(frame_hashes): h.update(le64(i)+fh)
    return h.digest()

PROBE = r'''#include "renderer_internal.h"
#include "odm_fixed.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void hx(const uint8_t*p,size_t n){size_t i;for(i=0;i<n;++i)printf("%02x",p[i]);}
static void node_init(odm_score_node*n,uint32_t id,uint32_t scene,uint32_t role,uint32_t cap,uint32_t res,int64_t start,int64_t end,int32_t z){memset(n,0,sizeof(*n));n->node_id=id;n->scene_id=scene;n->role=role;n->capability_id=cap;n->capability_major=1u;n->resource_id=res;n->state_model=ODM_STATELESS;n->z_index=z;n->start_sample=start;n->end_sample=end;n->scale_x_micro=ODM_MICRO_ONE;n->scale_y_micro=ODM_MICRO_ONE;n->opacity_micro=ODM_MICRO_ONE;n->seed_domain=id;}
int main(void){
 unsigned ci;
 for(ci=0;ci<256;ci++)printf("C,%u,%d,%d,%d\n",ci,(int)odm_renderer_decode_u8((uint8_t)ci,ODM_COLOR_TRANSFER_SRGB),(int)odm_renderer_decode_u8((uint8_t)ci,ODM_COLOR_TRANSFER_LINEAR),(int)odm_renderer_alpha_u8((uint8_t)ci));
 odm_score_header h={0};odm_score_resource rs[2]={{0}};odm_score_act act={0};odm_score_scene scenes[2]={{0}};odm_score_node nodes[5];odm_score_transition tr={0};odm_score_view score={0};odm_music_map_binding music={0};uint64_t ir_bytes=0u;uint8_t*ir=NULL;odm_render_ir_info iri;
 static const uint8_t ring[64]={0,0,0,0,255,220,120,255,255,220,120,255,0,0,0,0,255,220,120,255,0,0,0,0,0,0,0,0,255,220,120,255,255,220,120,255,0,0,0,0,0,0,0,0,255,220,120,255,0,0,0,0,255,220,120,255,255,220,120,255,0,0,0,0};
 static const uint8_t vred[16]={255,0,0,255,255,0,0,255,255,0,0,255,255,0,0,255};
 static const uint8_t vblue[16]={0,0,255,255,0,0,255,255,0,0,255,255,0,0,255,255};
 odm_render_surface_frame image={0},vf[2]={{0}};odm_render_resource_view rv[2]={{0}};odm_render_asset_set assets={0};odm_sha256_digest asha,rootsha;odm_reference_frame_root_context root;odm_reference_frame_info fi[6];uint64_t req[6]={0},fb=0,maxscratch=0;uint8_t*scratch=NULL;uint8_t frame[512];unsigned i;
 h.schema_version_major=ODM_SCORE_SCHEMA_MAJOR;h.schema_version_minor=ODM_SCORE_SCHEMA_MINOR;h.fps=30;h.width=8;h.height=8;h.project_end_sample=9600;h.project_seed=77;h.resource_count=2;h.act_count=1;h.scene_count=2;h.node_count=5;h.transition_count=1;
 rs[0].resource_id=1;rs[0].kind=ODM_SCORE_RESOURCE_IMAGE;if(odm_sha256("ring-source",11,&rs[0].content_sha256))return 2;rs[1].resource_id=2;rs[1].kind=ODM_SCORE_RESOURCE_VIDEO;if(odm_sha256("video-source",12,&rs[1].content_sha256))return 3;
 act.act_id=1;act.end_sample=9600;scenes[0].scene_id=1;scenes[0].act_id=1;scenes[0].environment_node_id=1;scenes[0].primary_node_id=2;scenes[0].end_sample=8000;scenes[1].scene_id=2;scenes[1].act_id=1;scenes[1].environment_node_id=4;scenes[1].primary_node_id=5;scenes[1].start_sample=4800;scenes[1].end_sample=9600;
 node_init(&nodes[0],1,1,ODM_SCORE_NODE_ENVIRONMENT,ODM_CAP_BLACK_VOID,0,0,8000,-100);node_init(&nodes[1],2,1,ODM_SCORE_NODE_PRIMARY,ODM_CAP_PRIMARY_IMAGE,1,0,8000,0);node_init(&nodes[2],3,1,ODM_SCORE_NODE_SUBJECT,ODM_CAP_SUBJECT_IMAGE,1,0,8000,10);nodes[2].scale_x_micro=625000;nodes[2].scale_y_micro=375000;nodes[2].opacity_micro=600000;nodes[2].phase_microturns=125000;node_init(&nodes[3],4,2,ODM_SCORE_NODE_ENVIRONMENT,ODM_CAP_BLACK_VOID,0,4800,9600,-100);node_init(&nodes[4],5,2,ODM_SCORE_NODE_PRIMARY,ODM_CAP_PRIMARY_VIDEO,2,4800,9600,0);
 tr.transition_id=1;tr.from_scene_id=1;tr.to_scene_id=2;tr.kind=ODM_TRANSITION_CROSSFADE;tr.start_sample=4800;tr.end_sample=8000;score.header=&h;score.resources=rs;score.acts=&act;score.scenes=scenes;score.nodes=nodes;score.transitions=&tr;
 music.canonical_frames=9600;if(odm_sha256("render-map",10,&music.music_map_sha256)||odm_music_policy_current_sha256(&music.music_policy_sha256))return 4;if(odm_score_compile_render_ir(&score,&music,NULL,0,&ir_bytes,NULL)!=ODM_STATUS_BUFFER_TOO_SMALL)return 5;ir=(uint8_t*)malloc((size_t)ir_bytes);if(!ir)return 6;if(odm_score_compile_render_ir(&score,&music,ir,ir_bytes,&ir_bytes,NULL)!=ODM_STATUS_OK)return 7;if(odm_render_ir_validate(ir,ir_bytes,NULL,&music.music_policy_sha256,&iri)!=ODM_STATUS_OK)return 8;
 image.width=4;image.height=4;image.pixel_format=ODM_RENDER_SURFACE_RGBA8;image.primaries=ODM_COLOR_PRIMARIES_BT709;image.transfer=ODM_COLOR_TRANSFER_SRGB;image.alpha_mode=ODM_ALPHA_STRAIGHT;image.pixel_bytes=64;image.pixels=ring;for(i=0;i<2;i++){vf[i].width=2;vf[i].height=2;vf[i].pixel_format=ODM_RENDER_SURFACE_RGBA8;vf[i].primaries=ODM_COLOR_PRIMARIES_BT709;vf[i].transfer=ODM_COLOR_TRANSFER_LINEAR;vf[i].alpha_mode=ODM_ALPHA_STRAIGHT;vf[i].start_sample=(int64_t)i*2400;vf[i].end_sample=(int64_t)(i+1)*2400;vf[i].pixel_bytes=16;vf[i].pixels=i?vblue:vred;}
 {static const uint32_t phases[5]={0u,0x40000000u,0x80000000u,0xc0000000u,0x13579bdfu};unsigned pi;for(pi=0;pi<5;pi++){odm_frame_node_state ns={0};odm_linear_pixel px;ns.scale_x_q32_32=ODM_Q32_ONE;ns.scale_y_q32_32=ODM_Q32_ONE;ns.opacity_q31=INT32_MAX;ns.phase=phases[pi];if(odm_renderer_sample_node(&ns,&image,1u,2u,8u,8u,&px)!=ODM_STATUS_OK)return 16;printf("S,%u,%d,%d,%d,%d\n",phases[pi],(int)px.r,(int)px.g,(int)px.b,(int)px.a);}}
 rv[0].resource_id=1;rv[0].kind=ODM_SCORE_RESOURCE_IMAGE;rv[0].frame_count=1;rv[0].source_sha256=rs[0].content_sha256;rv[0].frames=&image;rv[1].resource_id=2;rv[1].kind=ODM_SCORE_RESOURCE_VIDEO;rv[1].frame_count=2;rv[1].source_sha256=rs[1].content_sha256;rv[1].frames=vf;assets.resource_count=2;assets.resources=rv;if(odm_render_asset_set_sha256(&assets,&asha)!=ODM_STATUS_OK)return 9;
 printf("IR,");hx(iri.record_sha256.bytes,32);printf("\nASSET,");hx(asha.bytes,32);putchar('\n');
 for(i=0;i<6;i++){uint64_t sb=0;if(odm_reference_render_requirements(ir,ir_bytes,(int64_t)i,NULL,&fb,&sb)!=ODM_STATUS_OK||fb!=512)return 10;if(sb>maxscratch)maxscratch=sb;req[i]=sb;}scratch=(uint8_t*)malloc((size_t)maxscratch);if(!scratch)return 11;
 for(i=0;i<6;i++){uint64_t gotfb=0,gotsb=0;if(odm_reference_render_frame(ir,ir_bytes,(int64_t)i,NULL,&assets,NULL,scratch,maxscratch,frame,sizeof(frame),&gotfb,&gotsb,&fi[i])!=ODM_STATUS_OK)return 12;printf("F,%u,%lld,%llu,",i,(long long)fi[i].sample,(unsigned long long)req[i]);hx(fi[i].pixel_sha256.bytes,32);putchar(',');hx(fi[i].frame_sha256.bytes,32);putchar(',');hx(frame,sizeof(frame));putchar('\n');}
 if(odm_reference_frame_root_init(&root,&iri,&asha)!=ODM_STATUS_OK)return 13;
 for(i=0;i<6;i++){if(odm_reference_frame_root_update(&root,&fi[i])!=ODM_STATUS_OK)return 14;}
 if(odm_reference_frame_root_final(&root,&rootsha)!=ODM_STATUS_OK)return 15;
 printf("ROOT,");hx(rootsha.bytes,32);putchar('\n');
 free(scratch);free(ir);return 0;
}'''


def main() -> None:
    ap=argparse.ArgumentParser();ap.add_argument('--root',required=True);ap.add_argument('--cc',default='gcc');ap.add_argument('--library',required=True);args=ap.parse_args()
    root=pathlib.Path(args.root).resolve();lib=pathlib.Path(args.library).resolve()
    with tempfile.TemporaryDirectory(prefix='odm_renderer_oracle_') as td:
        td=pathlib.Path(td);c=td/'probe.c';exe=td/'probe';c.write_text(PROBE + '\n')
        subprocess.run([args.cc,'-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-I',str(root/'include'),'-I',str(root/'src/renderer'),'-I',str(root/'src/ir'),str(c),str(lib),'-pthread','-lpng16','-lssl','-lcrypto','-o',str(exe)],check=True,cwd=root)
        run=subprocess.run([str(exe)],check=True,cwd=root,text=True,capture_output=True)
    lines=[x.strip().split(',') for x in run.stdout.splitlines() if x.strip()]
    colors=[r for r in lines if r[0]=='C']; samples=[r for r in lines if r[0]=='S']; core=[r for r in lines if r[0] not in ('C','S')]
    if len(colors)!=256 or len(samples)!=5 or len(core)!=9 or core[0][0]!='IR' or core[1][0]!='ASSET' or core[-1][0]!='ROOT':
        raise SystemExit(f'probe output malformed: {run.stdout[:1000]}')
    for row in colors:
        i=int(row[1]); exp_s=SRGB[i]; exp_l=div_round_away(i*Q27,255); exp_a=alpha_u8(i)
        got=(int(row[2]),int(row[3]),int(row[4])); exp=(exp_s,exp_l,exp_a)
        if got!=exp: raise SystemExit(f'color vector {i} mismatch got={got} expected={exp}')
    for row in samples:
        phase=int(row[1]); exp=sample_surface(RING,4,4,TRANSFER_SRGB,1,2,Q32,Q32,I32_MAX,phase)
        got=tuple(map(int,row[2:6]))
        if got!=exp: raise SystemExit(f'sampler phase {phase:#x} mismatch got={got} expected={exp}')
    lines=core
    irsha=bytes.fromhex(lines[0][1]); got_asset=bytes.fromhex(lines[1][1]); exp_asset=asset_hash()
    if got_asset!=exp_asset: raise SystemExit(f'asset SHA mismatch got={got_asset.hex()} expected={exp_asset.hex()}')
    golden_dir=root/'tests'/'golden'/'renderer_v1'
    if not golden_dir.is_dir(): raise SystemExit('renderer golden directory missing')
    frame_hashes=[]; pixel_count=0
    for i in range(FRAME_COUNT):
        row=lines[2+i]
        if len(row)!=7 or row[0]!='F' or int(row[1])!=i: raise SystemExit(f'bad frame line {i}: {row}')
        sample,pixels=expected_frame(i); got_sample=int(row[2])
        golden=(golden_dir/f'frame_{i:04d}.rgba16le').read_bytes()
        if golden!=pixels: raise SystemExit(f'golden frame {i} drifted from independent mathematical oracle')
        if got_sample!=sample: raise SystemExit(f'frame {i} sample mismatch got={got_sample} expected={sample}')
        got_pix_sha=bytes.fromhex(row[4]);got_frame_sha=bytes.fromhex(row[5]);got_pixels=bytes.fromhex(row[6])
        if got_pixels!=pixels:
            idx=next((j for j,(a,b) in enumerate(zip(got_pixels,pixels)) if a!=b),min(len(got_pixels),len(pixels)))
            raise SystemExit(f'frame {i} pixel mismatch at byte {idx}: got={got_pixels[idx:idx+16].hex()} expected={pixels[idx:idx+16].hex()}')
        exp_pix_sha=hashlib.sha256(pixels).digest()
        if got_pix_sha!=exp_pix_sha: raise SystemExit(f'frame {i} pixel SHA mismatch')
        exp_fh=frame_hash(irsha,exp_asset,i,sample,pixels)
        if got_frame_sha!=exp_fh: raise SystemExit(f'frame {i} frame SHA mismatch got={got_frame_sha.hex()} expected={exp_fh.hex()}')
        frame_hashes.append(exp_fh);pixel_count += WIDTH*HEIGHT
    exp_root=root_hash(irsha,exp_asset,frame_hashes);got_root=bytes.fromhex(lines[-1][1])
    if got_root!=exp_root: raise SystemExit(f'frame root mismatch got={got_root.hex()} expected={exp_root.hex()}')
    print(f'Renderer oracle: pass (256 color vectors, 5 transform samples, {FRAME_COUNT} frames, {pixel_count} pixels / {pixel_count*4} channels exact)')
    print(f'Asset set SHA-256: {exp_asset.hex()}')
    for i,fh in enumerate(frame_hashes): print(f'Frame {i} SHA-256: {fh.hex()}')
    print(f'Frame root SHA-256: {exp_root.hex()}')

if __name__=='__main__': main()
