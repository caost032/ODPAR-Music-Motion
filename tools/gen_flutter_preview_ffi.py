#!/usr/bin/env python3
"""Generate the frozen Dart FFI surface for Gate-8 preview ABI v1."""
from __future__ import annotations
import argparse, json, pathlib
T='''// GENERATED FILE. DO NOT EDIT. Source: tests/golden/preview_ffi_v1.json
// Gate 8 binding. Native layout is frozen by preview_ffi_v1.json.
import 'dart:ffi';

const int odmFfiAbiVersion = {ffi};
const int odmPreviewAbiSchemaVersion = {schema};
const int odmFfiEndianMarker = 0x01020304;
const int odmPreviewFeatureBits = {features};

final class OdmSha256Digest extends Struct {{
  @Array(32) external Array<Uint8> bytes;
}}

final class OdmProgressSnapshot extends Struct {{
  @Uint32() external int schemaVersion;
  @Int32() external int state;
  @Int32() external int phase;
  @Int32() external int errorCode;
  @Uint32() external int cancelRequested;
  @Uint32() external int progressMillionths;
  @Uint32() external int reserved;
  @Uint64() external int generation;
  @Uint64() external int workDone;
  @Uint64() external int workTotal;
}}

final class OdmMusicAnalysisTick extends Struct {{
  @Uint64() external int tickIndex;
  @Uint64() external int centerSample;
  @Int32() external int rmsLeftQ31;
  @Int32() external int rmsRightQ31;
  @Int32() external int rmsMidQ31;
  @Int32() external int rmsSideQ31;
  @Array(6) external Array<Uint32> bandEnergyQ31;
  @Uint32() external int totalEnergyQ31;
  @Uint32() external int envelopeShortQ31;
  @Uint32() external int envelopeMediumQ31;
  @Uint32() external int envelopeLongQ31;
  @Uint32() external int spectralFluxQ31;
  @Uint32() external int spectralCentroidBinQ16_16;
  @Int64() external int energyDeltaQ31;
  @Uint32() external int silence;
  @Int32() external int stereoBalanceQ31;
  @Uint32() external int stereoWidthQ31;
  @Array(7) external Array<Uint32> reserved;
}}

final class OdmPreviewConfig extends Struct {{
  @Uint32() external int schemaVersion;
  @Uint32() external int rasterClass;
  @Uint32() external int pixelFormat;
  @Uint32() external int flags;
  @Array(4) external Array<Uint32> reserved;
}}

final class OdmPreviewAssetResource extends Struct {{
  @Uint32() external int resourceId;
  @Uint32() external int kind;
  @Uint32() external int frameCount;
  @Uint32() external int firstFrame;
  @Uint32() external int flags;
  @Uint32() external int reserved0;
  @Uint32() external int reserved1;
  @Uint32() external int reserved2;
  external OdmSha256Digest sourceSha256;
}}

final class OdmPreviewAssetFrame extends Struct {{
  @Uint32() external int width; @Uint32() external int height;
  @Uint32() external int pixelFormat; @Uint32() external int primaries;
  @Uint32() external int transfer; @Uint32() external int alphaMode;
  @Uint32() external int flags; @Uint32() external int reserved0;
  @Int64() external int startSample; @Int64() external int endSample;
  @Uint64() external int pixelOffset; @Uint64() external int pixelBytes;
  @Array(4) external Array<Uint32> reserved;
}}

final class OdmPreviewPlan extends Struct {{
  @Uint32() external int schemaVersion; @Uint32() external int rasterClass;
  @Uint32() external int pixelFormat; @Uint32() external int semanticsClass;
  @Uint32() external int sourceWidth; @Uint32() external int sourceHeight;
  @Uint32() external int outputWidth; @Uint32() external int outputHeight;
  @Uint32() external int divisor; @Int32() external int fps;
  @Int64() external int frameIndex; @Int64() external int sample;
  @Uint64() external int canonicalFrameBytes; @Uint64() external int outputFrameBytes;
  @Uint64() external int scratchBytes; @Uint64() external int workUnits;
  external OdmSha256Digest renderIrSha256;
  @Array(2) external Array<Uint32> reserved;
}}

final class OdmPreviewFrameInfo extends Struct {{
  @Uint32() external int schemaVersion; @Uint32() external int rasterClass;
  @Uint32() external int pixelFormat; @Uint32() external int semanticsClass;
  @Uint32() external int sourceWidth; @Uint32() external int sourceHeight;
  @Uint32() external int outputWidth; @Uint32() external int outputHeight;
  @Int32() external int fps; @Uint32() external int flags;
  @Int64() external int frameIndex; @Int64() external int sample;
  @Uint64() external int outputBytes;
  external OdmSha256Digest canonicalFrameSha256;
  external OdmSha256Digest previewPixelSha256;
}}

final class OdmFfiPreviewAbiInfo extends Struct {{
  @Uint32() external int structSize; @Uint32() external int schemaVersion;
  @Uint32() external int ffiAbiVersion; @Uint32() external int endianMarker;
  @Uint32() external int previewConfigSize; @Uint32() external int previewAssetResourceSize;
  @Uint32() external int previewAssetFrameSize; @Uint32() external int previewPlanSize;
  @Uint32() external int previewFrameInfoSize; @Uint32() external int progressSnapshotSize;
  @Uint32() external int jobStorageAlignment; @Uint32() external int musicAnalysisTickSize;
  @Uint64() external int jobStorageBytes; @Uint64() external int featureBits;
  @Array(8) external Array<Uint64> reservedU64;
}}

typedef _PreviewAbiNative = Int32 Function(Uint32, Pointer<OdmFfiPreviewAbiInfo>, Uint64, Pointer<Uint64>);
typedef PreviewAbi = int Function(int, Pointer<OdmFfiPreviewAbiInfo>, int, Pointer<Uint64>);
typedef _JobInitNative = Int32 Function(Pointer<Void>, Uint64);
typedef JobInit = int Function(Pointer<Void>, int);
typedef _JobBeginNative = Int32 Function(Pointer<Void>, Uint64, Uint64, Pointer<Uint64>);
typedef JobBegin = int Function(Pointer<Void>, int, int, Pointer<Uint64>);
typedef _JobCancelNative = Int32 Function(Pointer<Void>, Uint64);
typedef JobCancel = int Function(Pointer<Void>, int);
typedef _JobSnapshotNative = Int32 Function(Pointer<Void>, Uint64, Pointer<OdmProgressSnapshot>);
typedef JobSnapshot = int Function(Pointer<Void>, int, Pointer<OdmProgressSnapshot>);
typedef _JobFinishNative = Int32 Function(Pointer<Void>, Uint64, Uint64, Int32);
typedef JobFinish = int Function(Pointer<Void>, int, int, int);
typedef _JobDestroyNative = Int32 Function(Pointer<Void>, Uint64);
typedef JobDestroy = int Function(Pointer<Void>, int);

typedef _PreviewPlanFrameNative = Int32 Function(
    Pointer<Uint8>, Uint64, Int64, Pointer<OdmMusicAnalysisTick>,
    Pointer<OdmPreviewConfig>, Uint64, Uint64, Pointer<OdmPreviewPlan>);
typedef PreviewPlanFrame = int Function(
    Pointer<Uint8>, int, int, Pointer<OdmMusicAnalysisTick>,
    Pointer<OdmPreviewConfig>, int, int, Pointer<OdmPreviewPlan>);

typedef _PreviewRenderFrameNative = Int32 Function(
    Pointer<Uint8>, Uint64, Int64, Pointer<OdmMusicAnalysisTick>,
    Pointer<OdmPreviewConfig>, Pointer<OdmPreviewAssetResource>, Uint64,
    Pointer<OdmPreviewAssetFrame>, Uint64, Pointer<Uint8>, Uint64,
    Pointer<Void>, Uint64, Uint64, Pointer<Void>, Uint64, Pointer<Uint8>,
    Uint64, Pointer<Uint64>, Pointer<Uint64>, Pointer<OdmPreviewFrameInfo>);
typedef PreviewRenderFrame = int Function(
    Pointer<Uint8>, int, int, Pointer<OdmMusicAnalysisTick>,
    Pointer<OdmPreviewConfig>, Pointer<OdmPreviewAssetResource>, int,
    Pointer<OdmPreviewAssetFrame>, int, Pointer<Uint8>, int,
    Pointer<Void>, int, int, Pointer<Void>, int, Pointer<Uint8>,
    int, Pointer<Uint64>, Pointer<Uint64>, Pointer<OdmPreviewFrameInfo>);

class OdparMusicPreviewApi {{
  OdparMusicPreviewApi(DynamicLibrary library)
      : previewAbi = library.lookupFunction<_PreviewAbiNative, PreviewAbi>('odm_ffi_preview_abi_query'),
        jobInit = library.lookupFunction<_JobInitNative, JobInit>('odm_ffi_job_init'),
        jobBegin = library.lookupFunction<_JobBeginNative, JobBegin>('odm_ffi_job_begin'),
        jobCancel = library.lookupFunction<_JobCancelNative, JobCancel>('odm_ffi_job_request_cancel'),
        jobSnapshot = library.lookupFunction<_JobSnapshotNative, JobSnapshot>('odm_ffi_job_snapshot'),
        jobFinish = library.lookupFunction<_JobFinishNative, JobFinish>('odm_ffi_job_finish'),
        jobDestroy = library.lookupFunction<_JobDestroyNative, JobDestroy>('odm_ffi_job_destroy'),
        previewPlanFrame = library.lookupFunction<_PreviewPlanFrameNative, PreviewPlanFrame>('odm_ffi_preview_plan_frame'),
        previewRenderFrame = library.lookupFunction<_PreviewRenderFrameNative, PreviewRenderFrame>('odm_ffi_preview_render_frame');
  final PreviewAbi previewAbi;
  final JobInit jobInit;
  final JobBegin jobBegin;
  final JobCancel jobCancel;
  final JobSnapshot jobSnapshot;
  final JobFinish jobFinish;
  final JobDestroy jobDestroy;
  final PreviewPlanFrame previewPlanFrame;
  final PreviewRenderFrame previewRenderFrame;
}}
'''
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--golden',required=True);ap.add_argument('--output',required=True);a=ap.parse_args();g=json.loads(pathlib.Path(a.golden).read_text());out=pathlib.Path(a.output);out.parent.mkdir(parents=True,exist_ok=True);out.write_text(T.format(ffi=g['ffi_abi_version'],schema=g['schema_version'],features=g['runtime_feature_bits']),newline='\n')
if __name__=='__main__':main()
