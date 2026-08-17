# ODPAR Music Motion — Android Engine Lab

Private Android test surface over the official ODPAR Music Motion C11 engine.

The build downloads canonical Drive file ID `117etA7LrenJFEpgzYeRhD19T_jlY-TmA`, verifies SHA-256 `64d786ca5850519a324d2aff70f26134ac25ff4edb959da943e093f82cc47fc8`, and refuses to build if the production surface is not exactly 182 non-CLI C translation units.

The viewport is rasterized by ODPAR Scene3D (`odm_raster3d_*`), not by Android/OpenGL. The Music Spine path runs the engine's media decode, canonical 48 kHz resampler, analysis and Music Reaction pipeline. Android is UI, file selection, JNI and framebuffer presentation only.

The final `libodpar_lab.so` explicitly receives every production object file, preserving engine code that the current UI has not exposed yet. Missing Android portability dependencies are build failures, not silently removed capabilities.
