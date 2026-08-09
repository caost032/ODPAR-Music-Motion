#!/usr/bin/env python3
import argparse, hashlib, json, os, pathlib, shutil, struct, subprocess, tempfile

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--root',default='.'); ap.add_argument('--cc',default='gcc'); ap.add_argument('--library',required=True); args=ap.parse_args()
    root=pathlib.Path(args.root).resolve(); lib=pathlib.Path(args.library).resolve()
    ffmpeg=shutil.which('ffmpeg'); ffprobe=shutil.which('ffprobe')
    if not ffmpeg or not ffprobe: raise SystemExit('BLOCKED: ffmpeg/ffprobe missing')
    with tempfile.TemporaryDirectory(prefix='odpar-export-run-') as td:
        td=pathlib.Path(td); exe=td/'helper'
        cmd=[args.cc,'-std=c11','-Wall','-Wextra','-Wpedantic','-Wconversion','-Wsign-conversion','-Wshadow','-Wstrict-prototypes','-Wmissing-prototypes','-Werror','-O2','-I',str(root/'include'),str(root/'tools/export_run_smoke_helper.c'),str(lib),'-pthread','-lpng16','-lssl','-lcrypto','-o',str(exe)]
        subprocess.run(cmd,check=True,cwd=root)
        out=subprocess.run([str(exe),str(td)],check=True,text=True,capture_output=True).stdout.strip()
        raw=(td/'argv.bin').read_bytes(); tokens=[x.decode() for x in raw.split(b'\0') if x]
        published=td/'export.final'; video=str(published/'video.rgba'); audio=str(published/'audio.s32le'); mp4=str(td/'out.mp4')
        repl={'{ODPAR_VIDEO_RGBA8}':video,'{ODPAR_AUDIO_S32LE}':audio,'{ODPAR_OUTPUT_MP4}':mp4}
        argv=[repl.get(x,x) for x in tokens]; argv[0]=ffmpeg
        assert '-shortest' not in argv
        subprocess.run(argv,check=True,cwd=td)
        probe=json.loads(subprocess.run([ffprobe,'-v','error','-count_frames','-show_streams','-show_format','-of','json',mp4],check=True,text=True,capture_output=True).stdout)
        vs=next(s for s in probe['streams'] if s['codec_type']=='video'); aus=next(s for s in probe['streams'] if s['codec_type']=='audio')
        assert int(vs['nb_read_frames'])==4, vs
        assert vs['width']==32 and vs['height']==32 and vs['r_frame_rate']=='30/1'
        assert int(aus['sample_rate'])==48000 and aus['channels']==2
        assert published.is_dir(), 'transactional export directory was not published'
        assert not (td/'export.part').exists(), 'staging directory survived commit'
        rec=(published/'receipt.bin').read_bytes(); assert len(rec)==512 and rec[:8]==b'ODMEXRN1'
        # Independent receipt reconstruction: offsets are canonical wire v2, not ctypes ABI.
        schema,flags,width,height,fps,pixel_format,audio_rate,audio_channels=struct.unpack_from('<IIIIiIII',rec,8)
        frame_count=struct.unpack_from('<Q',rec,40)[0]
        samples_per_frame,project_end,output_end=struct.unpack_from('<qqq',rec,48)
        video_bytes,audio_frames,audio_bytes=struct.unpack_from('<QQQ',rec,72)
        assert (schema,flags,width,height,fps,pixel_format,audio_rate,audio_channels)==(2,0,32,32,30,2,48000,2)
        assert (frame_count,samples_per_frame,project_end,output_end)==(4,1600,4801,6400)
        assert (video_bytes,audio_frames,audio_bytes)==(4*32*32*4,6400,6400*8)
        recipe_sha=rec[96:128]; export_policy_sha=rec[128:160]; policy_sha=rec[160:192]; config_sha=rec[192:224]
        canonical_pcm_sha=rec[224:256]; video_sha=rec[256:288]; audio_sha=rec[288:320]; run_sha=rec[320:352]
        assert any(recipe_sha) and any(export_policy_sha) and any(policy_sha) and any(config_sha) and any(canonical_pcm_sha)
        vraw=pathlib.Path(video).read_bytes(); araw=pathlib.Path(audio).read_bytes()
        assert len(vraw)==video_bytes and len(araw)==audio_bytes
        # First 4,801 PCM frames are provider data; every frame thereafter is exact digital silence.
        def pcm_frame(i): return struct.unpack_from('<ii',araw,i*8)
        assert pcm_frame(0)==(0,0)
        assert pcm_frame(1)==(1000,-1000)
        assert pcm_frame(4800)==(4_800_000,-4_800_000)
        assert pcm_frame(4801)==(0,0) and pcm_frame(6399)==(0,0)
        expected_audio=bytearray()
        for i in range(4801):
            expected_audio += struct.pack('<ii', i*1000, -(i*1000))
        expected_audio += b'\0'*((6400-4801)*8)
        assert araw==bytes(expected_audio), 'raw S32LE source/padding bytes differ'
        assert araw[4801*8:]==b'\0'*((6400-4801)*8)
        source_pcm=bytes(expected_audio[:4801*8])
        expect_pcm=hashlib.sha256(b'ODPAR_EXPORT_CANONICAL_PCM_Q31_SOURCE_V1\0'+struct.pack('<Q',4801)+source_pcm).digest()
        expect_v=hashlib.sha256(b'ODPAR_EXPORT_RGBA8_STREAM_V1\0'+recipe_sha+struct.pack('<Q',4)+vraw).digest()
        expect_a=hashlib.sha256(b'ODPAR_EXPORT_S32LE_STREAM_V1\0'+recipe_sha+struct.pack('<Q',6400)+araw).digest()
        assert canonical_pcm_sha==expect_pcm, (canonical_pcm_sha.hex(),expect_pcm.hex())
        assert video_sha==expect_v, (video_sha.hex(),expect_v.hex())
        assert audio_sha==expect_a, (audio_sha.hex(),expect_a.hex())
        tmp=bytearray(rec); tmp[320:352]=b'\0'*32
        expect_run=hashlib.sha256(b'ODPAR_EXPORT_RUN_RECEIPT_V1\0'+tmp).digest()
        assert run_sha==expect_run, (run_sha.hex(), expect_run.hex())
        assert run_sha!=hashlib.sha256(tmp).digest(), 'receipt identity must remain domain-separated'
        assert rec[352:]==b'\0'*(512-352)
        print('EXPORT RUN SMOKE PASS')
        print(out)
        print('video_sha256='+hashlib.sha256(pathlib.Path(video).read_bytes()).hexdigest())
        print('audio_sha256='+hashlib.sha256(pathlib.Path(audio).read_bytes()).hexdigest())
        print('mp4_sha256='+hashlib.sha256(pathlib.Path(mp4).read_bytes()).hexdigest())
        print('receipt_sha256='+hashlib.sha256(rec).hexdigest())
        print('ffprobe_frames='+vs['nb_read_frames']+' audio_rate='+aus['sample_rate'])
if __name__=='__main__': main()
