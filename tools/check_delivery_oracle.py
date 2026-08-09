#!/usr/bin/env python3
"""Independent Gate 11 Delivery contract oracle.

C is used only to emit a legitimate Gate9 receipt and the Gate11 records. Python
parses ODMC itself and independently recomputes descriptor, contract, artifact,
payload and record SHA-256 identities.
"""
from __future__ import annotations
import argparse, hashlib, os, struct, subprocess, tempfile
from pathlib import Path

DESC_DOMAIN=b"ODPAR_DELIVERY_DESCRIPTOR_V1\0"
CONTRACT_DOMAIN=b"ODPAR_DELIVERY_CONTRACT_V1\0"
ARTIFACT_DOMAIN=b"ODPAR_DELIVERY_ARTIFACT_V1\0"
VIDEO=b"ffmpeg/libx264"
AUDIO=b"ffmpeg/aac"
SETTINGS=b"mp4;h264;yuv420p;bt709;crf=18;aac=192k;ar=48000"
FILE_BYTES=b"ODPAR Gate11 oracle delivered bytes\n"

def h(b: bytes)->bytes: return hashlib.sha256(b).digest()
def u32(v:int)->bytes:return struct.pack('<I',v&0xffffffff)
def u64(v:int)->bytes:return struct.pack('<Q',v&0xffffffffffffffff)
def i32(b:bytes,o:int)->int:return struct.unpack_from('<i',b,o)[0]
def q32(b:bytes,o:int)->int:return struct.unpack_from('<I',b,o)[0]
def q64(b:bytes,o:int)->int:return struct.unpack_from('<Q',b,o)[0]
def qi64(b:bytes,o:int)->int:return struct.unpack_from('<q',b,o)[0]

def descriptor(kind:int, s:bytes)->bytes:
    return h(DESC_DOMAIN+u32(kind)+u32(len(s))+s)

def odmc(record:bytes, *, kind:int, payload_len:int)->tuple[bytes,bytes]:
    assert len(record)==64+payload_len
    assert record[:4]==b'ODMC'
    assert struct.unpack_from('<HH',record,4)==(1,0)
    assert q32(record,8)==kind and q32(record,12)==0
    assert struct.unpack_from('<HH',record,16)==(1,0)
    assert q32(record,20)==0 and q64(record,24)==payload_len
    payload=record[64:]
    assert record[32:64]==h(payload)
    return payload,h(record)

PROBE=r'''#include "odm_delivery.h"
#include "odm_hash.h"
#include "odm_master.h"
#include "master_internal.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static odm_status hh(const char*s,odm_sha256_digest*d){return odm_sha256((const uint8_t*)s,(uint64_t)strlen(s),d);}
static int wr(const char*p,const uint8_t*b,size_t n){FILE*f=fopen(p,"wb");if(!f)return 0;size_t w=fwrite(b,1,n,f);return fclose(f)==0&&w==n;}
int main(int argc,char**argv){
 if(argc!=4)return 2;
 static const uint8_t vd[]="ffmpeg/libx264",ad[]="ffmpeg/aac",sd[]="mp4;h264;yuv420p;bt709;crf=18;aac=192k;ar=48000";
 static const uint8_t fb[]="ODPAR Gate11 oracle delivered bytes\n";
 odm_master_plan p;odm_render_receipt_info ri;odm_sha256_digest rs,v,a,s,fs;odm_delivery_contract_info ci;odm_delivery_artifact_info ai;
 uint8_t r[ODM_RENDER_RECEIPT_RECORD_BYTES],c[ODM_DELIVERY_CONTRACT_RECORD_BYTES],x[ODM_DELIVERY_ARTIFACT_RECORD_BYTES];uint64_t need=0;odm_status st;
 memset(&p,0,sizeof(p));p.schema_version_major=1;p.output_profile=ODM_MASTER_PROFILE_REFERENCE_V1;p.pixel_format=ODM_REFERENCE_PIXEL_RGBA16LE_LINEAR_PREMUL;
 p.audio_format=ODM_MASTER_AUDIO_PCM_Q31_STEREO_LE;p.work_schema=ODM_MASTER_WORK_SCHEMA_OUTPUT_VOLUME_V1;p.renderer_version_major=ODM_REFERENCE_RENDERER_VERSION_MAJOR;
 p.renderer_version_minor=ODM_REFERENCE_RENDERER_VERSION_MINOR;p.width=4;p.height=4;p.fps=30;p.frame_count=2;p.samples_per_frame=1600;p.project_end_sample=3200;p.output_end_sample=3200;p.canonical_music_frames=1600;p.project_seed=UINT64_C(0x1122334455667788);
 #define D(f,z) do{st=hh((z),&p.f);if(st!=ODM_STATUS_OK)return 10;}while(0)
 D(package_sha256,"g11-package");D(score_sha256,"g11-score");D(capability_sha256,"g11-cap");D(music_map_sha256,"g11-map");D(music_policy_sha256,"g11-policy");D(render_ir_sha256,"g11-ir");D(assets_sha256,"g11-assets");D(canonical_pcm_sha256,"g11-pcm");
 #undef D
 if(odm_master_render_id_internal(&p,&p.render_id)!=ODM_STATUS_OK)return 11;
 memset(&ri,0,sizeof(ri));ri.schema_version_major=1;ri.output_profile=p.output_profile;ri.pixel_format=p.pixel_format;ri.audio_format=p.audio_format;ri.work_schema=p.work_schema;ri.renderer_version_major=p.renderer_version_major;ri.renderer_version_minor=p.renderer_version_minor;ri.width=p.width;ri.height=p.height;ri.fps=p.fps;ri.frame_count=p.frame_count;ri.samples_per_frame=p.samples_per_frame;ri.project_end_sample=p.project_end_sample;ri.output_end_sample=p.output_end_sample;ri.canonical_music_frames=p.canonical_music_frames;ri.quoted_work_units=3232;ri.observed_work_units=3232;ri.project_seed=p.project_seed;ri.package_sha256=p.package_sha256;ri.score_sha256=p.score_sha256;ri.capability_sha256=p.capability_sha256;ri.music_map_sha256=p.music_map_sha256;ri.music_policy_sha256=p.music_policy_sha256;ri.render_ir_sha256=p.render_ir_sha256;ri.assets_sha256=p.assets_sha256;ri.canonical_pcm_sha256=p.canonical_pcm_sha256;ri.render_id=p.render_id;
 if(hh("g11-soundtrack",&ri.soundtrack_sha256)!=ODM_STATUS_OK||hh("g11-root",&ri.frame_root_sha256)!=ODM_STATUS_OK)return 12;
 if(odm_render_receipt_write_internal(&ri,r,sizeof(r),&need,&rs)!=ODM_STATUS_OK||need!=sizeof(r))return 13;
 if(odm_delivery_descriptor_sha256(1,vd,sizeof(vd)-1,&v)!=ODM_STATUS_OK||odm_delivery_descriptor_sha256(2,ad,sizeof(ad)-1,&a)!=ODM_STATUS_OK||odm_delivery_descriptor_sha256(3,sd,sizeof(sd)-1,&s)!=ODM_STATUS_OK)return 14;
 if(odm_delivery_contract_write(r,sizeof(r),&v,&a,&s,c,sizeof(c),&need,&ci)!=ODM_STATUS_OK||need!=sizeof(c))return 15;
 if(odm_sha256(fb,sizeof(fb)-1,&fs)!=ODM_STATUS_OK)return 16;
 if(odm_delivery_artifact_write(c,sizeof(c),&fs,sizeof(fb)-1,x,sizeof(x),&need,&ai)!=ODM_STATUS_OK||need!=sizeof(x))return 17;
 if(!wr(argv[1],r,sizeof(r))||!wr(argv[2],c,sizeof(c))||!wr(argv[3],x,sizeof(x)))return 18;
 return 0;
}'''

def compile_probe(root:Path,cc:str,lib:Path,tmp:Path)->Path:
    c=tmp/'probe.c'; exe=tmp/'probe'; c.write_text(PROBE+'\n')
    cmd=[cc,'-std=c11','-Wall','-Wextra','-Wpedantic','-Wconversion','-Wsign-conversion','-Wshadow','-Wstrict-prototypes','-Wmissing-prototypes','-Werror','-Iinclude','-Isrc/master',str(c),str(lib),'-pthread','-lpng16','-lssl','-lcrypto','-o',str(exe)]
    subprocess.run(cmd,cwd=root,check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    return exe

def main()->int:
    ap=argparse.ArgumentParser();ap.add_argument('--root',type=Path,required=True);ap.add_argument('--cc',default='gcc');ap.add_argument('--library',type=Path,required=True);a=ap.parse_args()
    root=a.root.resolve(); lib=a.library if a.library.is_absolute() else (root/a.library)
    with tempfile.TemporaryDirectory(prefix='odm-g11-oracle-') as d:
        td=Path(d);exe=compile_probe(root,a.cc,lib,td);rp,cp,apth=td/'receipt.bin',td/'contract.bin',td/'artifact.bin'
        subprocess.run([str(exe),str(rp),str(cp),str(apth)],cwd=root,check=True)
        receipt,contract,artifact=rp.read_bytes(),cp.read_bytes(),apth.read_bytes()
    _,receipt_sha=odmc(receipt,kind=0x54504352,payload_len=512)
    p,contract_sha=odmc(contract,kind=0x52564c44,payload_len=320)
    assert p[:8]==b'ODMDLVR1' and p[272:]==bytes(48)
    scalars=list(struct.unpack_from('<14I',p,8)); major,minor,container,vcodec,acodec,color,pix,rate,width,height,fps_u,audio_sr,flags,res0=scalars
    frame_count=q64(p,64); output_end=qi64(p,72)
    render_id=p[80:112]; embedded_receipt=p[112:144]; video=p[144:176]; audio=p[176:208]; settings=p[208:240]; cid=p[240:272]
    assert embedded_receipt==receipt_sha
    assert video==descriptor(1,VIDEO); assert audio==descriptor(2,AUDIO); assert settings==descriptor(3,SETTINGS)
    contract_material=(CONTRACT_DOMAIN+b''.join(u32(x) for x in scalars)+u64(frame_count)+u64(output_end)+render_id+embedded_receipt+video+audio+settings)
    expect_cid=h(contract_material); assert cid==expect_cid
    x,artifact_sha=odmc(artifact,kind=0x41564c44,payload_len=192)
    assert x[:8]==b'ODMDLVA1' and x[128:]==bytes(64)
    amaj,amin,aflags,ares=struct.unpack_from('<4I',x,8); fbytes=q64(x,24); acid=x[32:64]; fsha=x[64:96]; aid=x[96:128]
    assert acid==cid and fbytes==len(FILE_BYTES) and fsha==h(FILE_BYTES)
    expect_aid=h(ARTIFACT_DOMAIN+u32(amaj)+u32(amin)+u32(aflags)+u32(ares)+u64(fbytes)+acid+fsha);assert aid==expect_aid
    assert (major,minor,container,vcodec,acodec,color,pix,rate,width,height,fps_u,audio_sr,flags,res0)==(1,0,1,1,1,1,1,1,4,4,30,48000,0,0)
    print('Delivery oracle: pass (receipt + 384-byte contract + 256-byte artifact)')
    print('video_descriptor_sha256='+video.hex());print('audio_descriptor_sha256='+audio.hex());print('settings_sha256='+settings.hex())
    print('delivery_contract_id='+cid.hex());print('contract_record_sha256='+contract_sha.hex())
    print('artifact_id='+aid.hex());print('artifact_record_sha256='+artifact_sha.hex())
    return 0
if __name__=='__main__':raise SystemExit(main())
