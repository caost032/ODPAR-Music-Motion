#!/usr/bin/env python3
"""Independent Gate-9 canonical master / Render Receipt oracle.

A C probe uses only public ODPAR Music APIs to build one tiny signed project and
run the headless master. Python independently reconstructs Score/capability/PCM/
asset identities, canonical frame bytes and hashes, frame-root, soundtrack,
render_id, ODMC receipt integrity, and verifies the controller Ed25519 signature.
"""
from __future__ import annotations
import argparse, hashlib, pathlib, struct, subprocess, tempfile

SEED = 0x1122334455667788
# Identidad de la politica de musica v2. NO se toma del arbol bajo prueba:
# es la que reconstruye byte a byte tools/check_music_analysis_oracle.py a
# partir de la especificacion. La v1 valia cbe9ccd3...; cambio al publicar
# las razones con signo en el rango simetrico [-INT32_MAX, +INT32_MAX].
POLICY_SHA = bytes.fromhex('0a038fb13f700d4002a3b534dff1e4414c081a67caf63454079f9367ce23fdac')
I32_MAX=(1<<31)-1

PROBE=r'''#include "odm_master.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct { const char *dir; uint64_t next_audio; uint32_t frames; } sink_state;
static int save(const char *p,const void *d,size_t n){FILE*f=fopen(p,"wb");if(!f)return 0;int ok=fwrite(d,1,n,f)==n;ok=ok&&fclose(f)==0;return ok;}
static int path(char*out,size_t cap,const char*dir,const char*name){int n=snprintf(out,cap,"%s/%s",dir,name);return n>0&&(size_t)n<cap;}
static odm_status sbegin(void*u,const odm_master_plan*p){(void)p;return u?ODM_STATUS_OK:ODM_STATUS_INVALID_ARGUMENT;}
static odm_status sframe(void*u,const odm_reference_frame_info*i,const uint8_t*p,uint64_t n){sink_state*s=(sink_state*)u;char q[512];if(!s||!i||!p||(uint64_t)i->frame_index!=s->frames||n!=(uint64_t)128)return ODM_STATUS_INVALID_DATA;if(!path(q,sizeof(q),s->dir,s->frames==0u?"frame0.bin":"frame1.bin")||!save(q,p,(size_t)n))return ODM_STATUS_INTERNAL_ERROR;s->frames++;return ODM_STATUS_OK;}
static odm_status saudio(void*u,uint64_t start,const uint8_t*p,uint64_t n){sink_state*s=(sink_state*)u;char q[512];FILE*f;if(!s||!p||start!=s->next_audio||!path(q,sizeof(q),s->dir,"audio.bin"))return ODM_STATUS_INVALID_DATA;f=fopen(q,start==0u?"wb":"ab");if(!f)return ODM_STATUS_INTERNAL_ERROR;if(fwrite(p,8u,(size_t)n,f)!=(size_t)n){fclose(f);return ODM_STATUS_INTERNAL_ERROR;}if(fclose(f)!=0)return ODM_STATUS_INTERNAL_ERROR;s->next_audio+=n;return ODM_STATUS_OK;}
static odm_status scommit(void*u,const uint8_t*r,uint64_t n){(void)u;(void)r;return n==ODM_RENDER_RECEIPT_RECORD_BYTES?ODM_STATUS_OK:ODM_STATUS_INVALID_DATA;}
static void sabort(void*u,odm_status r){(void)u;(void)r;}
static void node(odm_score_node*n,uint32_t id,uint32_t role,uint32_t cap,int32_t z){memset(n,0,sizeof(*n));n->node_id=id;n->scene_id=1u;n->role=role;n->capability_id=cap;n->capability_major=1u;n->state_model=ODM_STATELESS;n->z_index=z;n->end_sample=3200;n->scale_x_micro=ODM_MICRO_ONE;n->scale_y_micro=ODM_MICRO_ONE;n->opacity_micro=ODM_MICRO_ONE;n->seed_domain=id;}
int main(int argc,char**argv){
 static const uint8_t package_seed[32]={0x9d,0x61,0xb1,0x9d,0xef,0xfd,0x5a,0x60,0xba,0x84,0x4a,0xf4,0x92,0xec,0x2c,0xc4,0x44,0x49,0xc5,0x69,0x7b,0x32,0x69,0x19,0x70,0x3b,0xac,0x03,0x1c,0xae,0x7f,0x60};
 static const uint8_t controller_seed[32]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32};
 odm_score_header h={0};odm_score_act a={0};odm_score_scene sc={0};odm_score_node ns[2];odm_score_view score={0};odm_music_analysis_plan ap={0};int32_t*w=NULL;odm_music_complex_q31*tw=NULL;odm_music_analysis_tick ticks[4]={{0}};odm_pcm_stereo_q31*pcm=NULL;odm_sha256_digest pcmsha,mapsha;uint8_t*analysis=NULL,*ir=NULL,*pkg=NULL,*frame=NULL,*scratch=NULL;uint64_t ab=0,ib=0,pb=0,rb=0;odm_music_map_binding bind={0};odm_odparms_info pi={0};odm_render_asset_set assets={0};odm_master_request req={0};odm_master_plan plan={0};odm_master_sink sink={0};sink_state ss={0};uint8_t receipt[ODM_RENDER_RECEIPT_RECORD_BYTES],env[ODM_CONTROLLER_SIGNATURE_BYTES],pk[32];char q[512];uint64_t i;int rc=2;
 if(argc!=2){return 2;}
 w=malloc((size_t)ODM_MUSIC_WINDOW_SAMPLES*sizeof(*w));tw=malloc((size_t)ODM_MUSIC_FFT_TWIDDLES*sizeof(*tw));pcm=calloc(1600u,sizeof(*pcm));if(!w||!tw||!pcm)goto done;if(odm_music_analysis_plan_build(w,ODM_MUSIC_WINDOW_SAMPLES,tw,ODM_MUSIC_FFT_TWIDDLES,&ap)!=ODM_STATUS_OK)goto done;for(i=0;i<4;i++){ticks[i].tick_index=i;ticks[i].center_sample=i*ODM_MUSIC_TICK_SAMPLES;ticks[i].silence=1u;}if(odm_master_pcm_sha256(pcm,1600u,&pcmsha)!=ODM_STATUS_OK)goto done;if(odm_music_map_record_write(&ap,1600u,&pcmsha,ticks,4u,NULL,0u,&ab,NULL)!=ODM_STATUS_BUFFER_TOO_SMALL)goto done;analysis=malloc((size_t)ab);if(!analysis||odm_music_map_record_write(&ap,1600u,&pcmsha,ticks,4u,analysis,ab,&ab,&mapsha)!=ODM_STATUS_OK)goto done;
 h.schema_version_major=ODM_SCORE_SCHEMA_MAJOR;h.schema_version_minor=ODM_SCORE_SCHEMA_MINOR;h.fps=30;h.width=4;h.height=4;h.project_end_sample=3200;h.project_seed=UINT64_C(0x1122334455667788);h.act_count=1;h.scene_count=1;h.node_count=2;a.act_id=1;a.end_sample=3200;sc.scene_id=1;sc.act_id=1;sc.environment_node_id=1;sc.primary_node_id=2;sc.end_sample=3200;node(&ns[0],1,ODM_SCORE_NODE_ENVIRONMENT,ODM_CAP_BLACK_VOID,-100);node(&ns[1],2,ODM_SCORE_NODE_PRIMARY,ODM_CAP_PRIMARY_EMPTY,0);score.header=&h;score.acts=&a;score.scenes=&sc;score.nodes=ns;if(odm_score_validate(&score)!=ODM_STATUS_OK)goto done;bind.canonical_frames=1600;bind.music_map_sha256=mapsha;bind.music_policy_sha256=ap.policy_sha256;if(odm_score_compile_render_ir(&score,&bind,NULL,0,&ib,NULL)!=ODM_STATUS_BUFFER_TOO_SMALL)goto done;ir=malloc((size_t)ib);if(!ir||odm_score_compile_render_ir(&score,&bind,ir,ib,&ib,NULL)!=ODM_STATUS_OK)goto done;if(odm_odparms_build(&score,ir,ib,analysis,ab,NULL,0,package_seed,NULL,0,&pb,&pi)!=ODM_STATUS_BUFFER_TOO_SMALL)goto done;pkg=malloc((size_t)pb);if(!pkg||odm_odparms_build(&score,ir,ib,analysis,ab,NULL,0,package_seed,pkg,pb,&pb,&pi)!=ODM_STATUS_OK)goto done;
 req.package_bytes=pkg;req.package_size=pb;req.expected_package_public_key=pi.signer_public_key;req.score=&score;req.assets=&assets;req.canonical_pcm=pcm;req.canonical_pcm_frames=1600;req.output_profile=ODM_MASTER_PROFILE_REFERENCE_V1;if(odm_master_plan_build(&req,&plan)!=ODM_STATUS_OK)goto done;frame=malloc((size_t)plan.frame_bytes);scratch=malloc((size_t)plan.renderer_scratch_bytes);if(!frame||!scratch)goto done;ss.dir=argv[1];sink.user=&ss;sink.begin=sbegin;sink.write_frame=sframe;sink.write_audio=saudio;sink.commit=scommit;sink.abort=sabort;if(odm_master_run(&req,&sink,frame,plan.frame_bytes,scratch,plan.renderer_scratch_bytes,receipt,sizeof(receipt),&rb,NULL)!=ODM_STATUS_OK)goto done;if(odm_controller_signature_public_from_seed(controller_seed,pk)!=ODM_STATUS_OK||odm_controller_signature_write(controller_seed,receipt,rb,env)!=ODM_STATUS_OK)goto done;
 if(!path(q,sizeof(q),argv[1],"package.bin")||!save(q,pkg,(size_t)pb))goto done;
 if(!path(q,sizeof(q),argv[1],"analysis.bin")||!save(q,analysis,(size_t)ab))goto done;
 if(!path(q,sizeof(q),argv[1],"render.ir")||!save(q,ir,(size_t)ib))goto done;
 if(!path(q,sizeof(q),argv[1],"receipt.bin")||!save(q,receipt,(size_t)rb))goto done;
 if(!path(q,sizeof(q),argv[1],"controller.bin")||!save(q,env,sizeof(env)))goto done;
 if(!path(q,sizeof(q),argv[1],"controller.pk")||!save(q,pk,sizeof(pk)))goto done;
 printf("frames=%u audio=%llu quote=%llu output=%llu scratch=%llu\\n",ss.frames,(unsigned long long)ss.next_audio,(unsigned long long)plan.quoted_work_units,(unsigned long long)plan.quoted_output_bytes,(unsigned long long)plan.renderer_scratch_bytes);rc=0;
 done:free(scratch);free(frame);free(pkg);free(ir);free(analysis);free(pcm);free(tw);free(w);return rc;
}
'''

def u32(b,o): return struct.unpack_from('<I',b,o)[0]
def u64(b,o): return struct.unpack_from('<Q',b,o)[0]
def i32(b,o): return struct.unpack_from('<i',b,o)[0]
def i64(b,o): return struct.unpack_from('<q',b,o)[0]
def le32(v): return struct.pack('<I',v & 0xffffffff)
def lei32(v): return struct.pack('<i',v)
def le64(v): return struct.pack('<Q',v & ((1<<64)-1))
def lei64(v): return struct.pack('<q',v)

def score_hash():
 h=hashlib.sha256();h.update(b'ODPAR_SCORE_V1\0')
 h.update(le32(1)+le32(1)+le32(0)+lei32(30)+le32(4)+le32(4)+lei64(3200)+le64(SEED))
 h.update(le32(0)+le32(1)+le32(1)+le32(2)+le32(0)+le32(0)+le32(0)+le32(0)+le32(0))
 h.update(le32(1)+le32(0)+lei64(0)+lei64(3200))
 h.update(le32(1)+le32(1)+le32(1)+le32(2)+lei64(0)+lei64(3200)+le32(0))
 for nid,role,cap,z in [(1,1,0x10001,-100),(2,2,0x10002,0)]:
  h.update(le32(nid)+le32(1)+le32(role)+le32(cap)+le32(1)+le32(0)+le32(0)+le32(0)+lei32(z)+le32(0))
  h.update(lei64(0)+lei64(3200)+lei64(0)+lei64(0)+lei64(1_000_000)+lei64(1_000_000)+lei64(1_000_000)+lei64(0)+le64(nid))
  h.update(le32(0)+le32(0)+le32(0)+le32(0)+le32(0))
 return h.digest()

def cap_hash():
 h=hashlib.sha256();h.update(b'ODPAR_CAPS_V1\0');h.update(le32(2))
 for cap,role in [(0x10001,1),(0x10002,2)]:h.update(le32(cap)+le32(1)+le32(0)+le32(0)+le32(1<<role)+le32(0))
 return h.digest()

def parse_receipt(r):
 if len(r)!=576 or r[:4]!=b'ODMC': raise AssertionError('receipt envelope')
 if (struct.unpack_from('<H',r,4)[0],struct.unpack_from('<H',r,6)[0],u32(r,8),u32(r,12),struct.unpack_from('<H',r,16)[0],struct.unpack_from('<H',r,18)[0],u32(r,20),u64(r,24))!=(1,0,0x54504352,0,1,0,0,512): raise AssertionError('ODMC header')
 p=r[64:];
 if hashlib.sha256(p).digest()!=r[32:64] or p[:8]!=b'ODMRCPT1' or any(p[472:]): raise AssertionError('receipt integrity')
 vals=dict(schema=(u32(p,8),u32(p,12)),output=u32(p,16),pixel=u32(p,20),audio=u32(p,24),work=u32(p,28),rmaj=u32(p,32),rmin=u32(p,36),width=u32(p,40),height=u32(p,44),fps=i32(p,48),flags=u32(p,52),frames=u64(p,56),spf=i64(p,64),project=i64(p,72),out=i64(p,80),music=u64(p,88),quoted=u64(p,96),observed=u64(p,104),seed=u64(p,112))
 names=['package','score','cap','map','policy','ir','assets','pcm','soundtrack','root','render_id']
 for j,n in enumerate(names): vals[n]=p[120+32*j:152+32*j]
 vals['record_sha']=hashlib.sha256(r).digest();return vals

def frame_sha(irsha,assetsha,index,pixels):
 h=hashlib.sha256();h.update(b'ODPAR_REF_FRAME_V1\0');h.update(irsha);h.update(assetsha);h.update(le32(1)+le32(0)+le32(30));h.update(lei64(index)+lei64(index*1600));h.update(le32(4)+le32(4)+le32(1)+le64(128));h.update(pixels);return h.digest()

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--root',required=True);ap.add_argument('--cc',default='gcc');ap.add_argument('--library',required=True);a=ap.parse_args();root=pathlib.Path(a.root).resolve();lib=pathlib.Path(a.library).resolve()
 with tempfile.TemporaryDirectory(prefix='odm_g9_oracle_') as td0:
  td=pathlib.Path(td0);c=td/'probe.c';exe=td/'probe';c.write_text(PROBE)
  cmd=[a.cc,'-std=c11','-Wall','-Wextra','-Wpedantic','-Wconversion','-Wsign-conversion','-Wshadow','-Wstrict-prototypes','-Wmissing-prototypes','-Werror','-I',str(root/'include'),str(c),str(lib),'-pthread','-lpng16','-lssl','-lcrypto','-o',str(exe)]
  subprocess.run(cmd,check=True,cwd=root);proc=subprocess.run([str(exe),str(td)],check=True,cwd=root,text=True,capture_output=True)
  package=(td/'package.bin').read_bytes();analysis=(td/'analysis.bin').read_bytes();ir=(td/'render.ir').read_bytes();receipt=(td/'receipt.bin').read_bytes();env=(td/'controller.bin').read_bytes();pk=(td/'controller.pk').read_bytes();frames=[(td/f'frame{i}.bin').read_bytes() for i in range(2)];audio=(td/'audio.bin').read_bytes()
 v=parse_receipt(receipt)
 if v['schema']!=(1,0) or (v['output'],v['pixel'],v['audio'],v['work'])!=(1,1,1,1) or (v['rmaj'],v['rmin'])!=(1,0): raise SystemExit('master profile drift')
 if (v['width'],v['height'],v['fps'],v['frames'],v['spf'],v['project'],v['out'],v['music'],v['quoted'],v['observed'],v['seed'])!=(4,4,30,2,1600,3200,3200,1600,3232,3232,SEED): raise SystemExit('receipt scalar mismatch')
 exp_frame=(b'\x00\x00'*3+b'\xff\xff')*16
 if frames!=[exp_frame,exp_frame]: raise SystemExit('canonical black frame bytes mismatch')
 if len(audio)!=3200*8 or any(audio): raise SystemExit('canonical soundtrack/padding mismatch')
 exp_assets=hashlib.sha256(b'ODPAR_RENDER_ASSETS_V1\0'+le32(0)).digest();exp_pcm=hashlib.sha256(b'ODPAR_PCM_Q31_STEREO_48K_V1\0'+le64(1600)+bytes(1600*8)).digest();exp_sound=hashlib.sha256(b'ODPAR_MASTER_SOUNDTRACK_V1\0'+le64(3200)+bytes(3200*8)).digest()
 checks={'package':hashlib.sha256(package).digest(),'score':score_hash(),'cap':cap_hash(),'map':hashlib.sha256(analysis).digest(),'policy':POLICY_SHA,'ir':hashlib.sha256(ir).digest(),'assets':exp_assets,'pcm':exp_pcm,'soundtrack':exp_sound}
 for n,x in checks.items():
  if v[n]!=x: raise SystemExit(f'{n} identity mismatch')
 fsha=[frame_sha(v['ir'],v['assets'],i,frames[i]) for i in range(2)]
 rh=hashlib.sha256();rh.update(b'ODPAR_REF_FRAME_ROOT_V1\0');rh.update(v['ir']);rh.update(v['assets']);rh.update(le32(1)+le32(0)+le32(1)+le32(4)+le32(4)+le32(30)+le64(2)+le64(3200));
 for i,x in enumerate(fsha):rh.update(le64(i)+x)
 if rh.digest()!=v['root']: raise SystemExit('frame root mismatch')
 rid=hashlib.sha256();rid.update(b'ODPAR_MASTER_RENDER_ID_V1\0')
 for n in ['package','score','cap','map','policy','ir','assets','pcm']:rid.update(v[n])
 rid.update(le32(1)+le32(1)+le32(1)+le32(1)+le32(1)+le32(0)+le32(4)+le32(4)+le32(30)+le64(2)+lei64(1600)+lei64(3200)+lei64(3200)+le64(1600)+le64(SEED))
 if rid.digest()!=v['render_id']: raise SystemExit('render_id mismatch')
 if env[:8]!=b'ODMCTRL1' or (u32(env,8),u32(env,12),u32(env,16),u32(env,20))!=(1,0,1,0) or env[24:56]!=v['record_sha'] or env[56:88]!=pk or any(env[152:]): raise SystemExit('controller envelope canonicality')
 try:
  from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
  Ed25519PublicKey.from_public_bytes(pk).verify(env[88:152],b'ODPAR_MASTER_RECEIPT_SIG_V1\0'+receipt)
 except Exception as e: raise SystemExit(f'independent controller Ed25519 verify failed: {e}')
 print(f'Master oracle: pass (2 frames, {len(frames[0])+len(frames[1])} video bytes, {len(audio)} audio bytes, 576-byte receipt)')
 print(f'Render ID SHA-256: {v["render_id"].hex()}')
 print(f'Frame root SHA-256: {v["root"].hex()}')
 print(f'Soundtrack SHA-256: {v["soundtrack"].hex()}')
 print(f'Receipt SHA-256: {v["record_sha"].hex()}')
 print(f'Controller public key: {pk.hex()}')
 print(proc.stdout.strip())
if __name__=='__main__':main()
