#!/usr/bin/env python3
"""Cross-check portable and opportunistic SHA-256 backends.

The production hash API never requires libcrypto.  When libcrypto is already
loaded and ODM_SHA256_OPENSSL_ACCEL is compiled in, the exact same public
context may delegate complete blocks to its optimized SHA implementation.
This oracle compiles both modes from the same hash.c and compares them to
Python hashlib across boundary sizes and incremental chunking.
"""
from __future__ import annotations
import argparse, hashlib, pathlib, re, subprocess, tempfile
HELPER=r'''#include "odm_hash.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
static void hx(const odm_sha256_digest*d){unsigned i;for(i=0;i<32u;++i)printf("%02x",d->bytes[i]);}
int main(int ac,char**av){size_t n,o=0;unsigned chunk;uint8_t*p;odm_sha256_context c=ODM_SHA256_CONTEXT_INITIALIZER;odm_sha256_digest d;if(ac!=3)return 2;n=(size_t)strtoull(av[1],0,10);chunk=(unsigned)strtoul(av[2],0,10);p=(uint8_t*)malloc(n?n:1u);if(!p)return 3;for(size_t i=0;i<n;++i)p[i]=(uint8_t)((i*131u+(i>>7)*17u+29u)&255u);if(odm_sha256_init(&c)!=ODM_STATUS_OK)return 4;if(chunk==0u)chunk=(unsigned)(n?n:1u);while(o<n){size_t z=n-o<chunk?n-o:chunk;if(odm_sha256_update(&c,p+o,z)!=ODM_STATUS_OK)return 5;o+=z;}if(odm_sha256_final(&c,&d)!=ODM_STATUS_OK)return 6;hx(&d);putchar('\n');free(p);return 0;}
'''
def run(cmd,cwd):
    r=subprocess.run(cmd,cwd=cwd,capture_output=True,text=True)
    if r.returncode: raise SystemExit('command failed\n'+' '.join(map(str,cmd))+'\n'+r.stdout+r.stderr)
    return r.stdout.strip()
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--root',required=True);ap.add_argument('--cc',default='gcc');a=ap.parse_args();root=pathlib.Path(a.root).resolve()
    sizes=[0,1,31,55,56,63,64,65,127,128,129,1024,4097,65536,1000003,4665600]
    chunks=[1,7,63,64,65,4096]
    with tempfile.TemporaryDirectory(prefix='odm-sha-backends-') as td:
        td=pathlib.Path(td);src=td/'h.c';src.write_text(HELPER+'\n');scalar=td/'scalar';provider=td/'provider'
        base=[a.cc,'-std=c11','-D_POSIX_C_SOURCE=200809L','-O2','-Wall','-Wextra','-Wpedantic','-Werror','-I',str(root/'include'),'-DODM_SHA256_OPENSSL_ACCEL=1',str(src),str(root/'src/core/hash.c')]
        run(base+['-DODM_SHA256_DISABLE_ACCEL=1','-o',str(scalar)],root)
        # --no-as-needed guarantees the weak provider symbol is actually resolved.
        run(base+['-Wl,--no-as-needed','-lcrypto','-Wl,--as-needed','-o',str(provider)],root)
        checked=0
        for n in sizes:
            data=bytes(((i*131+(i>>7)*17+29)&255) for i in range(n));exp=hashlib.sha256(data).hexdigest()
            use=[0]+([c for c in chunks if n<=4097] if n<=4097 else [63,4096])
            for ch in use:
                s=run([str(scalar),str(n),str(ch)],root);p=run([str(provider),str(n),str(ch)],root)
                if not re.fullmatch(r'[0-9a-f]{64}',s) or s!=p or s!=exp:
                    raise SystemExit(f'SHA backend mismatch n={n} chunk={ch} scalar={s} provider={p} expected={exp}')
                checked+=1
    print(f'SHA BACKEND ORACLE: PASS ({checked} size/chunk cases; scalar == provider == hashlib)')
    return 0
if __name__=='__main__':raise SystemExit(main())
