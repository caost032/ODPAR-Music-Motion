# ODPAR Music Motion — Android Engine Lab

Private Android test surface for the official ODPAR Music Motion C11 engine.

Build authority is canonical Drive file ID `117etA7LrenJFEpgzYeRhD19T_jlY-TmA`. The workflow verifies ZIP SHA-256 `64d786ca5850519a324d2aff70f26134ac25ff4edb959da943e093f82cc47fc8` before compiling and refuses to build unless the official production surface contains exactly 182 non-CLI C sources.

The 3D viewport is rasterized by ODPAR Scene3D (`odm_raster3d_*`), including engine camera, meshes, PBR-lite materials, lighting and shadow map. Android is only UI + JNI + framebuffer presentation. The APK also exposes engine identity, ABI/source hash, C selftest, stress rendering and the runtime Spine report.
