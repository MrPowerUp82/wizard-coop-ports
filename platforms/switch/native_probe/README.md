# Nintendo Switch native C++ CPU probe

This is a **real libnx/AArch64 build of the C++ game core**, intentionally without the renderer. Its job is to measure the simulation cost on actual Switch hardware before deko3d is introduced, so renderer cost and gameplay cost can be profiled separately.

It does not embed JavaScript, nx.js, HTML, Canvas, WebView or QuickJS.

## Build

Install/update devkitPro's `switch-dev` group, then from this directory:

```bash
make -j
```

Output: `arcana_switch_probe.nro`.

Run it from hbmenu. The screen reports the average native C++ core update time and live entity counts once per second.

Controls:

- Left stick: move
- B: dash
- A: special
- A / X / Y: choose a power
- Plus: exit

## Why a probe first?

The target performance problem has two independent costs: simulation and rendering. This probe lets us validate the C++/spatial-grid simulation on the Tegra X1 before adding the deko3d sprite batcher. The next Switch milestone is the GPU renderer: one atlas, a dynamic vertex ring buffer, compile-time UAM shaders and one/few draw calls per sprite layer.
