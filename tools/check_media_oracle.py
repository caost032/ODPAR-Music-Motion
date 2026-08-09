#!/usr/bin/env python3
"""Independent Media Truth oracle for canonical 48 kHz math, WAV Q1.31 and PNG RGBA8."""
from __future__ import annotations
import argparse, hashlib, json, os, random, struct, subprocess, tempfile, zlib
from pathlib import Path

PROBE_C = r'''
#include "odm_media.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *read_all(const char *path, uint64_t *out_n) {
    FILE *f = fopen(path, "rb"); long n; unsigned char *p;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    p = (unsigned char *)malloc(n == 0 ? 1u : (size_t)n);
    if (!p) { fclose(f); return NULL; }
    if (n != 0 && fread(p, 1u, (size_t)n, f) != (size_t)n) { free(p); fclose(f); return NULL; }
    fclose(f); *out_n = (uint64_t)n; return p;
}
static void hex_digest(const odm_sha256_digest *d, char out[65]) {
    uint64_t req = 0; if (odm_sha256_to_hex(d, out, 65u, &req) != ODM_STATUS_OK) strcpy(out, "ERR");
}
static void hex_bytes(const unsigned char *p, uint64_t n) {
    static const char h[]="0123456789abcdef"; uint64_t i;
    for (i=0;i<n;i++) { putchar(h[p[i]>>4]); putchar(h[p[i]&15u]); }
}
int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "duration") == 0) {
        uint64_t frames, got; unsigned rate;
        while (scanf("%" SCNu64 " %u", &frames, &rate) == 2) {
            odm_status s = odm_media_canonical_48k_frames(frames, (uint32_t)rate, &got);
            if (s == ODM_STATUS_OK) printf("0 %" PRIu64 "\n", got); else printf("%" PRId32 " 0\n", s);
        }
        return 0;
    }
    if (argc != 3) return 2;
    uint64_t n=0, required=0; unsigned char *src=read_all(argv[2], &n); odm_media_facts f;
    if (!src) return 3;
    if (strcmp(argv[1], "wav") == 0) {
        odm_pcm_stereo_q31 *frames = NULL; uint64_t i; odm_status s;
        memset(&f,0,sizeof(f));
        s=odm_media_decode_audio_q31(src,n,NULL,0,&required,&f);
        if (s != ODM_STATUS_BUFFER_TOO_SMALL) { printf("S %" PRId32 "\n",s); free(src); return 0; }
        frames=(odm_pcm_stereo_q31*)calloc(required ? (size_t)required:1u,sizeof(*frames)); if(!frames){free(src);return 4;}
        s=odm_media_decode_audio_q31(src,n,frames,required,&required,&f);
        if(s!=ODM_STATUS_OK){printf("S %" PRId32 "\n",s);free(frames);free(src);return 0;}
        char sh[65],dh[65],rh[65]; unsigned char rec[224]; uint64_t rr=0; odm_sha256_digest rd;
        hex_digest(&f.source_sha256,sh);hex_digest(&f.decoded_sha256,dh);
        s=odm_media_facts_write_record(&f,rec,sizeof(rec),&rr,&rd); if(s!=ODM_STATUS_OK){printf("S %" PRId32 "\n",s);free(frames);free(src);return 0;}
        hex_digest(&rd,rh);
        printf("OK %" PRIu64 " %u %u %u %u %" PRIu64 " %" PRIu64 " %" PRIu64 " %s %s %s ",required,f.channels,f.sample_rate,f.bits_per_sample,f.codec,f.source_frames,f.canonical_48k_frames,f.decoded_bytes,sh,dh,rh);
        for(i=0;i<required;i++){ if(i)putchar(','); printf("%" PRId32 ":%" PRId32,frames[i].left,frames[i].right); }
        putchar(' '); hex_bytes(rec,rr); putchar('\n');
        free(frames); free(src); return 0;
    }
    if (strcmp(argv[1], "png") == 0) {
        unsigned char *rgba=NULL; odm_status s; char sh[65],dh[65];
        memset(&f,0,sizeof(f)); s=odm_media_decode_image_rgba8(src,n,NULL,0,&required,&f);
        if(s!=ODM_STATUS_BUFFER_TOO_SMALL){printf("S %" PRId32 "\n",s);free(src);return 0;}
        rgba=(unsigned char*)malloc(required? (size_t)required:1u); if(!rgba){free(src);return 4;}
        s=odm_media_decode_image_rgba8(src,n,rgba,required,&required,&f);
        if(s!=ODM_STATUS_OK){printf("S %" PRId32 "\n",s);free(rgba);free(src);return 0;}
        hex_digest(&f.source_sha256,sh);hex_digest(&f.decoded_sha256,dh);
        printf("OK %u %u %" PRIu64 " %s %s ",f.width,f.height,required,sh,dh);hex_bytes(rgba,required);putchar('\n');
        free(rgba);free(src);return 0;
    }
    free(src); return 2;
}
'''

def run(cmd, *, cwd=None, input_text=None):
    p=subprocess.run(cmd,cwd=cwd,input=input_text,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    if p.returncode:
        raise RuntimeError(f"command failed {p.returncode}: {' '.join(map(str,cmd))}\n{p.stdout}\n{p.stderr}")
    return p.stdout

def ceil_div(n,d): return n//d + (1 if n%d else 0)

def q31_from_int(v,bits):
    if bits==8: return (v-128)<<24
    if bits==16: return v<<16
    if bits==24: return v<<8
    if bits==32: return v
    raise ValueError

def q31_from_ieee(bits,width):
    if width==32:
        sign=bits>>31; exp=(bits>>23)&0xff; frac=bits&0x7fffff; bias=127; mantbits=23
        if exp==0xff: raise ValueError
    else:
        sign=bits>>63; exp=(bits>>52)&0x7ff; frac=bits&0xfffffffffffff; bias=1023; mantbits=52
        if exp==0x7ff: raise ValueError
    if exp==0: return 0
    # exact positive magnitude |value|*2^31 as integer ratio
    mant=(1<<mantbits)|frac
    shift=(exp-bias)+31-mantbits
    if shift>=0: mag=mant<<shift
    else:
        d=1<<(-shift); q,r=divmod(mant,d); mag=q+(1 if r*2>=d else 0)
    mag=min(mag,1<<31)
    if sign: return -(1<<31) if mag >= 1<<31 else -mag
    return min(mag,(1<<31)-1)

def sample_bytes(v,bits,is_float=False):
    if is_float:
        return struct.pack('<I' if bits==32 else '<Q', v)
    if bits==8: return bytes([v])
    if bits==16: return struct.pack('<h',v)
    if bits==24:
        if v<0: v += 1<<24
        return bytes((v&255,(v>>8)&255,(v>>16)&255))
    return struct.pack('<i',v)

def make_wav(rate,channels,bits,vals,is_float=False,extensible=False):
    data=b''.join(sample_bytes(v,bits,is_float) for frame in vals for v in frame)
    tag=3 if is_float else 1
    if extensible:
        guid=struct.pack('<IHH8B',tag,0,0x0010,0x80,0,0,0xaa,0,0x38,0x9b,0x71)
        fmt=struct.pack('<HHIIHHHHI',0xfffe,channels,rate,rate*channels*(bits//8),channels*(bits//8),bits,22,bits,3 if channels==2 else 4)+guid
    else:
        fmt=struct.pack('<HHIIHH',tag,channels,rate,rate*channels*(bits//8),channels*(bits//8),bits)
    chunks=b'fmt '+struct.pack('<I',len(fmt))+fmt
    if len(fmt)&1: chunks+=b'\0'
    chunks+=b'data'+struct.pack('<I',len(data))+data
    if len(data)&1: chunks+=b'\0'
    return b'RIFF'+struct.pack('<I',4+len(chunks))+b'WAVE'+chunks

def expected_record(meta, srcsha, decsha):
    payload=struct.pack('<8I2Q4IQ',1,1,meta['codec'],1,meta['channels'],meta['rate'],meta['bits'],0,meta['frames'],meta['canon'],0,0,0,0,meta['frames']*8)
    payload+=bytes.fromhex(srcsha)+bytes.fromhex(decsha)+bytes(24)
    assert len(payload)==160
    pd=hashlib.sha256(payload).digest()
    head=b'ODMC'+struct.pack('<HHIIHHIQ',1,0,0x4341464d,0,1,0,0,len(payload))+pd
    assert len(head)==64
    return head+payload

def png_chunk(kind,data):
    return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)

def make_png(w,h,rgba):
    rows=b''.join(b'\0'+rgba[y*w*4:(y+1)*w*4] for y in range(h))
    return b'\x89PNG\r\n\x1a\n'+png_chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,6,0,0,0))+png_chunk(b'IDAT',zlib.compress(rows,9))+png_chunk(b'IEND',b'')

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--root',type=Path,required=True);ap.add_argument('--cc',default='gcc');ap.add_argument('--library',type=Path,required=True);args=ap.parse_args()
    root=args.root.resolve(); lib=(root/args.library).resolve() if not args.library.is_absolute() else args.library
    rng=random.Random(0x4f445041524d4544)
    with tempfile.TemporaryDirectory(prefix='odm-media-oracle-') as td_s:
        td=Path(td_s); c=td/'probe.c'; exe=td/'probe'; c.write_text(PROBE_C)
        png_libs=run(['pkg-config','--libs','libpng']).strip().split()
        run([args.cc,'-std=c11','-O2','-I'+str(root/'include'),str(c),str(lib),'-pthread',*png_libs,'-lssl','-lcrypto','-o',str(exe)])
        # 25,000 independent exact duration vectors + boundary cases.
        pairs=[(0,48000),(1,44100),((1<<64)-1,384000),((1<<64)-1,1)]
        rates=[8000,11025,16000,22050,32000,44100,48000,88200,96000,176400,192000,352800,384000]
        for _ in range(25000): pairs.append((rng.getrandbits(64),rng.choice(rates)))
        inp=''.join(f'{a} {r}\n' for a,r in pairs)
        lines=run([str(exe),'duration'],input_text=inp).splitlines()
        if len(lines)!=len(pairs): raise RuntimeError('duration probe line count mismatch')
        for (a,r),line in zip(pairs,lines):
            st_s,got_s=line.split(); st=int(st_s); got=int(got_s); n=a*48000; exp=ceil_div(n,r)
            if exp>(1<<64)-1:
                if st!=3: raise RuntimeError(f'duration overflow mismatch {a=} {r=} {st=} {got=}')
            elif st!=0 or got!=exp:
                raise RuntimeError(f'duration mismatch {a=} {r=} {got=} {exp=} {st=}')
        # WAV vectors cover integer/float widths, mono/stereo, extensible, rates and random samples.
        wav_count=0
        for is_float,bits in [(False,8),(False,16),(False,24),(False,32),(True,32),(True,64)]:
            for channels in (1,2):
                for rate in (8000,44100,48000,96000,384000):
                    vals=[]; expected=[]
                    for j in range(7):
                        frame=[]; out=[]
                        for ch in range(channels):
                            if is_float:
                                if bits==32:
                                    choices=[0x00000000,0x80000000,0x3f000000,0xbf000000,0x3f7fffff,0xbf800000,0x3e800000,0x3fc00000]
                                else:
                                    choices=[0,1<<63,0x3fe0000000000000,0xbfe0000000000000,0x3fefffffffffffff,0xbff0000000000000,0x3fd0000000000000,0x3ff8000000000000]
                                v=rng.choice(choices); q=q31_from_ieee(v,bits)
                            else:
                                if bits==8: v=rng.randrange(256)
                                else: v=rng.randrange(-(1<<(bits-1)),1<<(bits-1))
                                q=q31_from_int(v,bits)
                            frame.append(v);out.append(q)
                        vals.append(tuple(frame)); expected.append((out[0],out[0] if channels==1 else out[1]))
                    wav=make_wav(rate,channels,bits,vals,is_float,extensible=((wav_count%4)==0))
                    fp=td/f'v{wav_count}.wav';fp.write_bytes(wav)
                    line=run([str(exe),'wav',str(fp)]).strip(); parts=line.split(' ',13)
                    if not line.startswith('OK '): raise RuntimeError(f'WAV rejected: {line}')
                    _,req_s,ch_s,rate_s,bits_s,codec_s,frames_s,canon_s,db_s,srcsha,decsha,recsha,frame_txt,record_hex=parts
                    got_frames=[] if not frame_txt else [tuple(map(int,x.split(':'))) for x in frame_txt.split(',')]
                    canon=ceil_div(len(vals)*48000,rate)
                    meta={'codec':2 if is_float else 1,'channels':channels,'rate':rate,'bits':bits,'frames':len(vals),'canon':canon}
                    raw_dec=b''.join(struct.pack('<ii',*x) for x in expected)
                    if (int(req_s),int(ch_s),int(rate_s),int(bits_s),int(codec_s),int(frames_s),int(canon_s),int(db_s)) != (len(vals),channels,rate,bits,meta['codec'],len(vals),canon,len(vals)*8): raise RuntimeError('WAV facts mismatch')
                    if got_frames!=expected: raise RuntimeError(f'Q1.31 mismatch vector {wav_count}: {got_frames} != {expected}')
                    if srcsha!=hashlib.sha256(wav).hexdigest() or decsha!=hashlib.sha256(raw_dec).hexdigest(): raise RuntimeError('WAV digest mismatch')
                    exp_rec=expected_record(meta,srcsha,decsha)
                    if bytes.fromhex(record_hex)!=exp_rec or recsha!=hashlib.sha256(exp_rec).hexdigest(): raise RuntimeError('Media Facts ODMC mismatch')
                    wav_count+=1
        # PNG vectors: independently created zlib/CRC streams with exact RGBA bytes.
        png_count=0
        for w,h in [(1,1),(2,3),(7,5),(16,9)]:
            for _ in range(4):
                rgba=bytes(rng.randrange(256) for _ in range(w*h*4)); png=make_png(w,h,rgba); fp=td/f'p{png_count}.png';fp.write_bytes(png)
                line=run([str(exe),'png',str(fp)]).strip(); parts=line.split()
                if len(parts)!=7 or parts[0]!='OK': raise RuntimeError(f'PNG rejected: {line}')
                _,gw,gh,req,srcsha,decsha,rgbahex=parts
                if (int(gw),int(gh),int(req))!=(w,h,len(rgba)) or bytes.fromhex(rgbahex)!=rgba: raise RuntimeError('PNG pixel mismatch')
                if srcsha!=hashlib.sha256(png).hexdigest() or decsha!=hashlib.sha256(rgba).hexdigest(): raise RuntimeError('PNG digest mismatch')
                png_count+=1
        print(f'Media Truth independent oracle: pass ({len(pairs)} duration vectors, {wav_count} WAV vectors, {png_count} PNG vectors)')
    return 0
if __name__=='__main__': raise SystemExit(main())
