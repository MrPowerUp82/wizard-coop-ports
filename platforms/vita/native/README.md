# Arcana Survivors — native PS Vita runtime

This target does **not** embed QuickJS, HTML, Canvas or the old JS bridge. It links the shared C++ gameplay core directly to VitaSDK/libvita2d.

## Current milestone

- 60 Hz fixed-step simulation.
- Vita controls read directly through `SceCtrl`.
- Native sprite/circle rendering through libvita2d/GXM.
- One pre-baked 1792×1536 atlas; no WebP decode/recolor/canvas baking at runtime.
- Fixed-capacity hot entity arrays in the C++ core.
- Player movement, auto-attack, dash, special, enemies, drops, bosses and power choices run through the C++ simulation.

Controls in this first hardware test:

- Left stick / D-pad: move
- Circle: dash
- Cross: special
- During power choice: Cross/Square/Triangle = option 1/2/3
- Start: exit

## Build

Install VitaSDK and libvita2d, then:

```bash
vdpm install libvita2d
cmake -S platforms/vita/native -B build-vita \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-vita -j
```

Output: `build-vita/arcana-survivors.vpk`.

The next renderer pass will add phase floor tiles, full HUD/menu, effects batching and audio while preserving this allocation-free hot path.
