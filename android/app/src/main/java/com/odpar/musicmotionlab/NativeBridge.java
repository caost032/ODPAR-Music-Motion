package com.odpar.musicmotionlab;

final class NativeBridge {
    static { System.loadLibrary("odpar_lab"); }
    static native String engineStatus();
    static native String selftest();
    static native String spineSummary();
    static native String spineFull();
    static native int[] renderDemo(int width, int height, int panXmm, int panYmm, int zoomMm);
    static native String lastRenderMeta();
    private NativeBridge() {}
}
