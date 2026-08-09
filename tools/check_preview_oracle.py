#!/usr/bin/env python3
"""Independent Gate-8 raster-preview oracle, byte-exact against canonical renderer math."""
from __future__ import annotations
import argparse, hashlib, pathlib, struct, subprocess, sys, tempfile
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import check_renderer_oracle as ro

CLASSES=((0,1),(1,2),(2,4),(3,8))
FRAMES=(0,4)

def avg_u16(total:int,count:int)->int: return (total+count//2)//count
def unpremul(v:int,a:int)->int:
    if a==0:return 0
    return min(65535,(v*65535+a//2)//a)
def linear_to_srgb8(v:int)->int:
    target=ro.div_round_away(v*ro.Q27,65535)
    lo=0;hi=255
    while lo<hi:
        mid=lo+(hi-lo)//2
        if ro.SRGB[mid]<target:lo=mid+1
        else:hi=mid
    if lo==0:return 0
    upper=ro.SRGB[lo]-target;lower=target-ro.SRGB[lo-1]
    return lo-1 if lower<upper else lo
def alpha8(a:int)->int:return (a*255+32767)//65535

def preview_expected(canonical:bytes,divisor:int)->tuple[int,int,bytes]:
    sw,sh=ro.WIDTH,ro.HEIGHT;ow=(sw+divisor-1)//divisor;oh=(sh+divisor-1)//divisor
    vals=[struct.unpack_from('<HHHH',canonical,i*8) for i in range(sw*sh)]
    out=bytearray()
    for oy in range(oh):
        sy0=oy*divisor;sy1=min(sh,sy0+divisor)
        for ox in range(ow):
            sx0=ox*divisor;sx1=min(sw,sx0+divisor)
            block=[vals[y*sw+x] for y in range(sy0,sy1) for x in range(sx0,sx1)]
            n=len(block); ar=avg_u16(sum(p[0] for p in block),n);ag=avg_u16(sum(p[1] for p in block),n);ab=avg_u16(sum(p[2] for p in block),n);aa=avg_u16(sum(p[3] for p in block),n)
            out += bytes((linear_to_srgb8(unpremul(ar,aa)),linear_to_srgb8(unpremul(ag,aa)),linear_to_srgb8(unpremul(ab,aa)),alpha8(aa)))
    return ow,oh,bytes(out)

PROBE=r'''#include "odm_preview.h"
#include "odm_fixed.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void hx(const uint8_t*p,size_t n){size_t i;for(i=0;i<n;++i)printf("%02x",p[i]);}
static void ni(odm_score_node*n,uint32_t id,uint32_t scene,uint32_t role,uint32_t cap,uint32_t res,int64_t start,int64_t end,int32_t z){memset(n,0,sizeof(*n));n->node_id=id;n->scene_id=scene;n->role=role;n->capability_id=cap;n->capability_major=1u;n->resource_id=res;n->state_model=ODM_STATELESS;n->z_index=z;n->start_sample=start;n->end_sample=end;n->scale_x_micro=ODM_MICRO_ONE;n->scale_y_micro=ODM_MICRO_ONE;n->opacity_micro=ODM_MICRO_ONE;n->seed_domain=id;}
int main(void){
 static const uint8_t ring[64]={0,0,0,0,255,220,120,255,255,220,120,255,0,0,0,0,255,220,120,255,0,0,0,0,0,0,0,0,255,220,120,255,255,220,120,255,0,0,0,0,0,0,0,0,255,220,120,255,0,0,0,0,255,220,120,255,255,220,120,255,0,0,0,0};
 static const uint8_t vred[16]={255,0,0,255,255,0,0,255,255,0,0,255,255,0,0,255};
 static const uint8_t vblue[16]={0,0,255,255,0,0,255,255,0,0,255,255,0,0,255,255};
 uint8_t blob[96];odm_score_header h={0};odm_score_resource rs[2]={{0}};odm_score_act act={0};odm_score_scene sc[2]={{0}};odm_score_node n[5];odm_score_transition tr={0};odm_score_view sv={0};odm_music_map_binding music={0};uint64_t ib=0;uint8_t*ir=NULL;odm_preview_asset_resource pr[2]={{0}};odm_preview_asset_frame pf[3]={{0}};unsigned fi,ci;
 memcpy(blob,ring,64);memcpy(blob+64,vred,16);memcpy(blob+80,vblue,16);
 h.schema_version_major=ODM_SCORE_SCHEMA_MAJOR;h.schema_version_minor=ODM_SCORE_SCHEMA_MINOR;h.fps=30;h.width=8;h.height=8;h.project_end_sample=9600;h.project_seed=77;h.resource_count=2;h.act_count=1;h.scene_count=2;h.node_count=5;h.transition_count=1;
 rs[0].resource_id=1;rs[0].kind=ODM_SCORE_RESOURCE_IMAGE;if(odm_sha256("ring-source",11,&rs[0].content_sha256))return 2;rs[1].resource_id=2;rs[1].kind=ODM_SCORE_RESOURCE_VIDEO;if(odm_sha256("video-source",12,&rs[1].content_sha256))return 3;
 act.act_id=1;act.end_sample=9600;sc[0].scene_id=1;sc[0].act_id=1;sc[0].environment_node_id=1;sc[0].primary_node_id=2;sc[0].end_sample=8000;sc[1].scene_id=2;sc[1].act_id=1;sc[1].environment_node_id=4;sc[1].primary_node_id=5;sc[1].start_sample=4800;sc[1].end_sample=9600;
 ni(&n[0],1,1,ODM_SCORE_NODE_ENVIRONMENT,ODM_CAP_BLACK_VOID,0,0,8000,-100);ni(&n[1],2,1,ODM_SCORE_NODE_PRIMARY,ODM_CAP_PRIMARY_IMAGE,1,0,8000,0);ni(&n[2],3,1,ODM_SCORE_NODE_SUBJECT,ODM_CAP_SUBJECT_IMAGE,1,0,8000,10);n[2].scale_x_micro=625000;n[2].scale_y_micro=375000;n[2].opacity_micro=600000;n[2].phase_microturns=125000;ni(&n[3],4,2,ODM_SCORE_NODE_ENVIRONMENT,ODM_CAP_BLACK_VOID,0,4800,9600,-100);ni(&n[4],5,2,ODM_SCORE_NODE_PRIMARY,ODM_CAP_PRIMARY_VIDEO,2,4800,9600,0);
 tr.transition_id=1;tr.from_scene_id=1;tr.to_scene_id=2;tr.kind=ODM_TRANSITION_CROSSFADE;tr.start_sample=4800;tr.end_sample=8000;sv.header=&h;sv.resources=rs;sv.acts=&act;sv.scenes=sc;sv.nodes=n;sv.transitions=&tr;music.canonical_frames=9600;if(odm_sha256("render-map",10,&music.music_map_sha256)||odm_music_policy_current_sha256(&music.music_policy_sha256))return 4;if(odm_score_compile_render_ir(&sv,&music,NULL,0,&ib,NULL)!=ODM_STATUS_BUFFER_TOO_SMALL)return 5;ir=malloc((size_t)ib);if(!ir)return 6;if(odm_score_compile_render_ir(&sv,&music,ir,ib,&ib,NULL)!=ODM_STATUS_OK)return 7;
 pr[0].resource_id=1;pr[0].kind=ODM_SCORE_RESOURCE_IMAGE;pr[0].frame_count=1;pr[0].first_frame=0;pr[0].source_sha256=rs[0].content_sha256;pr[1].resource_id=2;pr[1].kind=ODM_SCORE_RESOURCE_VIDEO;pr[1].frame_count=2;pr[1].first_frame=1;pr[1].source_sha256=rs[1].content_sha256;
 pf[0].width=4;pf[0].height=4;pf[0].pixel_format=ODM_RENDER_SURFACE_RGBA8;pf[0].primaries=ODM_COLOR_PRIMARIES_BT709;pf[0].transfer=ODM_COLOR_TRANSFER_SRGB;pf[0].alpha_mode=ODM_ALPHA_STRAIGHT;pf[0].pixel_offset=0;pf[0].pixel_bytes=64;
 pf[1].width=2;pf[1].height=2;pf[1].pixel_format=ODM_RENDER_SURFACE_RGBA8;pf[1].primaries=ODM_COLOR_PRIMARIES_BT709;pf[1].transfer=ODM_COLOR_TRANSFER_LINEAR;pf[1].alpha_mode=ODM_ALPHA_STRAIGHT;pf[1].start_sample=0;pf[1].end_sample=2400;pf[1].pixel_offset=64;pf[1].pixel_bytes=16;
 pf[2]=pf[1];pf[2].start_sample=2400;pf[2].end_sample=4800;pf[2].pixel_offset=80;
 for(fi=0;fi<2;fi++){int64_t frame_index=fi?4:0;for(ci=0;ci<4;ci++){odm_preview_config c={0};odm_preview_plan p;odm_preview_frame_info info;uint8_t*out=NULL,*scratch=NULL;uint64_t ro=0,rsq=0;c.schema_version=1;c.raster_class=ci;c.pixel_format=1;if(odm_preview_plan_frame(ir,ib,frame_index,NULL,&c,2,3,&p)!=ODM_STATUS_OK)return 8;out=malloc((size_t)p.output_frame_bytes);scratch=malloc((size_t)p.scratch_bytes);if(!out||!scratch)return 9;if(odm_preview_render_frame(ir,ib,frame_index,NULL,&c,pr,2,pf,3,blob,sizeof(blob),NULL,scratch,p.scratch_bytes,out,p.output_frame_bytes,&ro,&rsq,&info)!=ODM_STATUS_OK)return 10;printf("P,%lld,%u,%u,%u,",(long long)frame_index,ci,p.output_width,p.output_height);hx(info.canonical_frame_sha256.bytes,32);putchar(',');hx(info.preview_pixel_sha256.bytes,32);putchar(',');hx(out,(size_t)p.output_frame_bytes);putchar('\n');free(scratch);free(out);}}
 free(ir);return 0;}
'''

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--root',required=True);ap.add_argument('--cc',default='gcc');ap.add_argument('--library',required=True);a=ap.parse_args();root=pathlib.Path(a.root).resolve();lib=pathlib.Path(a.library).resolve()
    with tempfile.TemporaryDirectory(prefix='odm-preview-oracle-') as td:
        td=pathlib.Path(td);c=td/'p.c';exe=td/'p';c.write_text(PROBE+'\n',newline='\n')
        cmd=[a.cc,'-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-I',str(root/'include'),str(c),str(lib),'-pthread','-lpng16','-lssl','-lcrypto','-o',str(exe)]
        r=subprocess.run(cmd,cwd=root,capture_output=True,text=True)
        if r.returncode: raise SystemExit('preview oracle probe compile failed\n'+r.stdout+r.stderr)
        x=subprocess.run([str(exe)],cwd=root,capture_output=True,text=True)
        if x.returncode: raise SystemExit(f'preview oracle probe failed {x.returncode}\n{x.stderr}')
    rows=[ln.split(',') for ln in x.stdout.splitlines() if ln]
    if len(rows)!=len(FRAMES)*len(CLASSES): raise SystemExit(f'preview probe row count {len(rows)}')
    checked=0
    for row in rows:
        if len(row)!=8 or row[0]!='P': raise SystemExit(f'bad preview row {row}')
        fi=int(row[1]);klass=int(row[2]);ow=int(row[3]);oh=int(row[4]);got_c=bytes.fromhex(row[5]);got_p=bytes.fromhex(row[6]);got=bytes.fromhex(row[7])
        _,canonical=ro.expected_frame(fi); divisor=dict(CLASSES)[klass];eow,eoh,expected=preview_expected(canonical,divisor)
        if (ow,oh)!=(eow,eoh):raise SystemExit(f'geometry mismatch frame={fi} class={klass}')
        if got_c!=hashlib.sha256(canonical).digest():raise SystemExit(f'canonical hash mismatch frame={fi} class={klass}')
        if got!=expected:
            j=next((i for i,(u,v) in enumerate(zip(got,expected)) if u!=v),min(len(got),len(expected)))
            raise SystemExit(f'preview bytes mismatch frame={fi} class={klass} byte={j} got={got[j:j+16].hex()} exp={expected[j:j+16].hex()}')
        if got_p!=hashlib.sha256(expected).digest():raise SystemExit(f'preview hash mismatch frame={fi} class={klass}')
        checked+=len(expected)
    print(f'Preview oracle: pass ({len(rows)} frame/class vectors, {checked} RGBA8 bytes exact)')
    for fi in FRAMES:
        _,canonical=ro.expected_frame(fi);print(f'Canonical frame {fi} SHA-256: {hashlib.sha256(canonical).hexdigest()}')
if __name__=='__main__':main()
