# ODPAR Music Motion Studio — Android build intent

This marker exists only to trigger the dedicated Studio workflow after the workflow itself is present on the branch.

The build must use the canonical 182-source ODPAR Music Motion engine, package `com.odpar.musicmotion`, and native library `libodpar_studio.so`. It must not fall back to the legacy Android Engine Lab.
