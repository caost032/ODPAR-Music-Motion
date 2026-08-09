#!/usr/bin/env python3
"""Observed Gate 11 delivery-adapter smoke for this host only.

This is intentionally non-semantic: it demonstrates one local FFmpeg/libx264/AAC
installation can encode the frozen delivery profile. It does not enter render_id
and does not claim universal codec support, licensing conclusions, or cross-host
byte identity.
"""
from __future__ import annotations
import argparse, hashlib, json, shutil, struct, subprocess, tempfile, time
from pathlib import Path

D=b"ODPAR_DELIVERY_DESCRIPTOR_V1\0"
SETTINGS=(b"mp4;h264;libx264;preset=medium;crf=18;pix_fmt=yuv420p;color_range=tv;"
          b"color_space=bt709;color_primaries=bt709;color_trc=bt709;"
          b"x264_colorprim=bt709;x264_transfer=bt709;x264_colormatrix=bt709;"
          b"aac=192k;ar=48000;channels=2;movflags=+faststart")
def dh(k:int,s:bytes)->str:
    return hashlib.sha256(D+struct.pack('<II',k,len(s))+s).hexdigest()
def run(cmd:list[str])->subprocess.CompletedProcess[str]:
    return subprocess.run(cmd,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=True)
def main()->int:
    ap=argparse.ArgumentParser();ap.add_argument('--json-out',type=Path);a=ap.parse_args()
    ff=shutil.which('ffmpeg'); fp=shutil.which('ffprobe')
    if not ff or not fp: print('FFmpeg delivery smoke: BLOCKED_MISSING_FFMPEG'); return 2
    ver=run([ff,'-version']).stdout.splitlines(); cfg=next((x for x in ver if x.startswith('configuration:')), '')
    enc=run([ff,'-hide_banner','-encoders']).stdout
    if 'libx264' not in enc or ' aac ' not in enc: raise SystemExit('required local encoders unavailable')
    with tempfile.TemporaryDirectory(prefix='odm-g11-ffmpeg-') as d:
        td=Path(d); vr=td/'frames.rgba64le'; ar=td/'audio.s32le'; out=td/'delivery.mp4'
        px=struct.pack('<HHHH',0,0,0,65535); vr.write_bytes(px*(4*4*2)); ar.write_bytes(bytes(3200*2*4))
        cmd=[ff,'-hide_banner','-loglevel','error','-y','-f','rawvideo','-pixel_format','rgba64le','-video_size','4x4','-framerate','30','-i',str(vr),'-f','s32le','-ar','48000','-ac','2','-i',str(ar),'-c:v','libx264','-preset','medium','-crf','18','-pix_fmt','yuv420p','-color_range','tv','-colorspace','bt709','-color_primaries','bt709','-color_trc','bt709','-x264-params','colorprim=bt709:transfer=bt709:colormatrix=bt709','-c:a','aac','-b:a','192k','-ar','48000','-ac','2','-movflags','+faststart','-shortest',str(out)]
        t=time.monotonic_ns(); run(cmd); elapsed=time.monotonic_ns()-t
        probe=json.loads(run([fp,'-v','error','-show_entries','format=format_name:stream=index,codec_name,profile,width,height,pix_fmt,r_frame_rate,color_range,color_space,color_transfer,color_primaries,sample_rate,channels,channel_layout,nb_frames','-of','json',str(out)]).stdout)
        b=out.read_bytes(); fsha=hashlib.sha256(b).hexdigest()
    streams=probe.get('streams',[]); v=next(x for x in streams if x.get('codec_name')=='h264'); au=next(x for x in streams if x.get('codec_name')=='aac')
    assert v.get('width')==4 and v.get('height')==4 and v.get('pix_fmt')=='yuv420p' and v.get('r_frame_rate')=='30/1'
    assert v.get('color_range')=='tv' and v.get('color_space')=='bt709' and v.get('color_transfer')=='bt709' and v.get('color_primaries')=='bt709'
    assert au.get('sample_rate')=='48000' and au.get('channels')==2
    assert 'mp4' in probe.get('format',{}).get('format_name','')
    video_desc=(ver[0]+' | '+cfg+' | encoder=libx264').encode('ascii','replace')
    audio_desc=(ver[0]+' | '+cfg+' | encoder=aac').encode('ascii','replace')
    result={'schema':'odm_gate11_ffmpeg_observation_v1','status':'pass_local_observation','ffmpeg':ff,'ffprobe':fp,'ffmpeg_version':ver[0] if ver else '', 'ffmpeg_configuration':cfg,'gpl_enabled':'--enable-gpl' in cfg,'libx264_enabled':'--enable-libx264' in cfg,'output_file_bytes_observation_only':len(b),'output_file_sha256_observation_only':fsha,'elapsed_ns_observation_only':elapsed,'video_encoder_descriptor_sha256':dh(1,video_desc),'audio_encoder_descriptor_sha256':dh(2,audio_desc),'settings_descriptor_sha256':dh(3,SETTINGS),'probe':probe,'nonclaims':['not universal H.264/AAC certification','not byte-identical across FFmpeg versions or hosts','not a licensing opinion','not part of canonical render_id','elapsed time is nonsemantic']}
    if a.json_out:
        a.json_out.parent.mkdir(parents=True,exist_ok=True);a.json_out.write_text(json.dumps(result,indent=2,sort_keys=True)+'\n')
    print('FFmpeg delivery smoke: pass (local host observation only)')
    print(result['ffmpeg_version']);print(cfg)
    print('output_file_bytes='+str(len(b)));print('output_file_sha256='+fsha)
    print('video_encoder_descriptor_sha256='+result['video_encoder_descriptor_sha256']);print('audio_encoder_descriptor_sha256='+result['audio_encoder_descriptor_sha256']);print('settings_descriptor_sha256='+result['settings_descriptor_sha256'])
    return 0
if __name__=='__main__':raise SystemExit(main())
