GloDroid's minigbm patches (glodroid/configuration common/graphics/patches-minigbm),
rebased on minigbm 93e16b9 by hand. Changes against GloDroid's originals:
- 0006: drv_bo_create return-path hunk redone around the upstream log_bos block.
- 0012: backend_mock also guarded by DRV_DUMB (meson build doesn't compile backend_mock.c).
- 0017 dropped: upstream already strips drv_logv under NDEBUG; its b_ndebug=true
  meson.build change is folded into 0018.
- 0019 (new): #include <functional> in gbm_mesa_internals.cpp for current libc++.
- 0020 (new): meson builds for the AIDL allocator (gralloc5) and the stable-C mapper, renamed to the _gd suffix that GloDroid's aospext packaging expects (getIMapperLibrarySuffix, VINTF instance, rc path). GloDroid's 0010 meson.build only built gralloc0/4; Android 17 has no HIDL gralloc.
