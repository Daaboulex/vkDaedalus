# Performance

What vkDaedalus adds to a frame, measured by the live command on real GPUs. The tables at the end come from
[performance.tsv](performance.tsv), which `nix run .#study -- collect <live log>` writes from a run's log;
`nix run .#study -- write docs/performance.tsv docs/performance.md` puts them here, and a flake check fails
when this page and the data disagree.

## How a figure is taken

- **Per-frame cost**: vkcube runs 600 frames and then 1800 frames, and the difference of the two run times,
  divided by the 1200 frames between them, is one frame's time, so start-up and shut-down cancel out. Five
  runs without the layer alternate with five with it and one ReShade effect in the chain. Each figure is the
  median of the five, with the shortest and longest in brackets, in milliseconds per frame. The run uses
  the immediate present mode where the GPU offers it (present mode 0), so no refresh rate caps the frames;
  a GPU without it uses FIFO (present mode 2), where the display's refresh caps both runs and the
  difference says little. Where the run times vary by more than the frames between them cost, the
  shortest in brackets can fall below zero; such a figure is lost in that GPU's noise.
- **The HUD and the launch notice**: the same, with the HUD drawing FPS, frame time and the slowest 1%, or
  with the launch notice showing, against the layer without them.
- **Nothing in this run**: the frame time after Turn off in this run hands the swapchain back, against a run
  without the layer.
- **Half precision and instant sliders**: the per-frame cost of an effect that asks for half precision, and
  of a ReShade uniform changed live, each against its plain counterpart.
- **Queue drain against own fence**: a game frame, a layer pass carrying a fence and the game's next frame
  are submitted together; the over-wait is how much longer waiting for the whole queue takes than waiting
  on that fence, the time a layer that drained the queue would hold the game back.
- **Memory of one effect image**: the size of the image an effect renders into at two resolutions.

## Frame generation and FPS counters

With lsfg-vk at 2x the run counts the frames shown for every frame the game drew, and records the gaps
between presents on each side of lsfg-vk. lsfg-vk does not spread its presents evenly: below it the median
gap is a 60 Hz display's 16.67 ms but the shortest 1% are a small fraction of a millisecond, as the tables
below show, because lsfg-vk queues a generated frame right behind a real one. A counter that turns one gap into an FPS figure
then shows tens of thousands of FPS while the screen shows 120. Steam's overlay reporting around 10000 FPS
with frame generation fits a counter of that kind sitting below lsfg-vk; its place in the layer order is
not confirmed. vkDaedalus's own Performance card counts the presents over a second and over 10 seconds
instead, as [LATENCY.md](../LATENCY.md) describes.

## Measured

<!-- BEGIN generated:study -->

### NVIDIA GeForce GTX 1660 SUPER, driver 595.104.02

| Measured | Result |
| --- | --- |
| lsfg-vk switched live, effects before: gaps between presents below lsfg-vk | median 16.665 ms, shortest 1% 0.038 ms, shortest 0.030 ms, so a counter reading one gap would show up to 33546 FPS |
| lsfg-vk switched live, effects before: base | 2.00 frames shown for every frame the game drew, want 2 |
| lsfg-vk switched live, effects before: off | 0.99 frames shown for every frame the game drew, want 1 |
| lsfg-vk switched live, effects before: on | 2.00 frames shown for every frame the game drew, want 2 |
| lsfg-vk switched live, effects before: three | 3.03 frames shown for every frame the game drew, want 3 |
| lsfg-vk switched live, effects before: file-off | 1.01 frames shown for every frame the game drew, want 1 |
| lsfg-vk switched live, effects after: gaps between presents above lsfg-vk | median 16.672 ms, shortest 1% 16.033 ms, shortest 0.337 ms, so a counter reading one gap would show up to 2966 FPS |
| lsfg-vk switched live, effects after: base | 1.98 frames shown for every frame the game drew, want 2 |
| lsfg-vk switched live, effects after: off | 1.00 frames shown for every frame the game drew, want 1 |
| lsfg-vk switched live, effects after: on | 2.00 frames shown for every frame the game drew, want 2 |
| lsfg-vk switched live, effects after: three | 3.00 frames shown for every frame the game drew, want 3 |
| lsfg-vk switched live, effects after: file-off | 1.00 frames shown for every frame the game drew, want 1 |
| per-frame cost of none in this run | 0.1694 ms per frame from present 8873 on, after Turn off handed the swapchain back, against 0.1706 ms without the layer (present mode 0, xcb) |
| per-frame cost, one effect, wayland | 0.1357 (0.1094..0.1409) ms/frame without, 0.1361 (0.1283..0.3210) with, median of 5 at 1800 minus 600 frames (present mode 0) |
| per-frame cost, one effect, xcb | 0.1601 (0.1535..0.3901) ms/frame without, 0.1733 (0.1654..0.2673) with, median of 5 at 1800 minus 600 frames (present mode 0) |
| per-frame cost of the HUD, wayland | 0.1600 (0.1344..0.3881) ms/frame with the HUD drawing 3 lines (fps,frametime,slowest), 0.1395 (-0.1444..0.1435) without it, median of 5 at 1800 minus 600 frames (present mode 0) |
| per-frame cost of the launch notice, wayland | 0.1543 (-0.0313..0.2911) ms/frame while it shows, 0.1395 (-0.1444..0.1435) without it, median of 5 at 1800 minus 600 frames (present mode 0) |
| memory of one effect image | 1920x1080 8 MB, 3840x2160 31 MB; the layer keeps one per swapchain image for each effect, up to the effect limit |
| per-frame cost of half precision, wayland | 0.1815 (-0.1173..0.4297) ms/frame with half precision, 0.2194 (-0.0472..0.5009) with full, an effect doing 256 min16float multiply-adds a pixel, median of 5 at 1800 minus 600 frames (present mode 0) |
| per-frame cost of instant ReShade sliders, wayland | 0.1442 (0.1364..0.4315) ms/frame with instant sliders, 0.1389 (-0.1392..0.4237) without, uniform_smoke, median of 5 at 1800 minus 600 frames (present mode 0) |
| queue drain vs own fence | over-wait: mean 244.65 ms |

### AMD Radeon RX 9070 XT (RADV GFX1201), driver Mesa 26.3.0-devel

| Measured | Result |
| --- | --- |
| lsfg-vk switched live, effects before: gaps between presents below lsfg-vk | median 16.666 ms, shortest 1% 0.042 ms, shortest 0.026 ms, so a counter reading one gap would show up to 38285 FPS |
| lsfg-vk switched live, effects before: base | 2.00 frames shown for every frame the game drew, want 2 |
| lsfg-vk switched live, effects before: off | 1.00 frames shown for every frame the game drew, want 1 |
| lsfg-vk switched live, effects before: on | 2.00 frames shown for every frame the game drew, want 2 |
| lsfg-vk switched live, effects before: three | 2.95 frames shown for every frame the game drew, want 3 |
| lsfg-vk switched live, effects before: file-off | 0.99 frames shown for every frame the game drew, want 1 |
| lsfg-vk switched live, effects after: gaps between presents above lsfg-vk | median 16.672 ms, shortest 1% 16.561 ms, shortest 0.186 ms, so a counter reading one gap would show up to 5370 FPS |
| lsfg-vk switched live, effects after: base | 2.00 frames shown for every frame the game drew, want 2 |
| lsfg-vk switched live, effects after: off | 1.00 frames shown for every frame the game drew, want 1 |
| lsfg-vk switched live, effects after: on | 2.02 frames shown for every frame the game drew, want 2 |
| lsfg-vk switched live, effects after: three | 2.95 frames shown for every frame the game drew, want 3 |
| lsfg-vk switched live, effects after: file-off | 1.00 frames shown for every frame the game drew, want 1 |
| per-frame cost of none in this run | 0.0434 ms per frame from present 8313 on, after Turn off handed the swapchain back, against 0.0434 ms without the layer (present mode 0, xcb) |
| per-frame cost, one effect, wayland | 0.0239 (0.0216..0.0258) ms/frame without, 0.0348 (0.0283..0.0414) with, median of 5 at 1800 minus 600 frames (present mode 0) |
| per-frame cost, one effect, xcb | 0.0429 (0.0425..0.0508) ms/frame without, 0.0422 (0.0414..0.0557) with, median of 5 at 1800 minus 600 frames (present mode 0) |
| per-frame cost of the HUD, wayland | 0.0530 (0.0517..0.0538) ms/frame with the HUD drawing 3 lines (fps,frametime,slowest), 0.0354 (0.0311..0.0375) without it, median of 5 at 1800 minus 600 frames (present mode 0) |
| per-frame cost of the launch notice, wayland | 0.0441 (0.0426..0.0460) ms/frame while it shows, 0.0354 (0.0311..0.0375) without it, median of 5 at 1800 minus 600 frames (present mode 0) |
| memory of one effect image | 1920x1080 4 MB, 3840x2160 15 MB; the layer keeps one per swapchain image for each effect, up to the effect limit |
| per-frame cost of half precision, wayland | 0.0426 (0.0421..0.0446) ms/frame with half precision, 0.0432 (0.0420..0.0451) with full, an effect doing 256 min16float multiply-adds a pixel, median of 5 at 1800 minus 600 frames (present mode 0) |
| per-frame cost of instant ReShade sliders, wayland | 0.0304 (0.0298..0.0306) ms/frame with instant sliders, 0.0308 (0.0267..0.0316) without, uniform_smoke, median of 5 at 1800 minus 600 frames (present mode 0) |
| queue drain vs own fence | over-wait: mean 1.30 ms |

### AMD Ryzen 9 9950X3D 16-Core Processor (RADV RAPHAEL_MENDOCINO), driver Mesa 26.3.0-devel

| Measured | Result |
| --- | --- |
| lsfg-vk switched live, effects before: gaps between presents below lsfg-vk | median 16.664 ms, shortest 1% 0.038 ms, shortest 0.028 ms, so a counter reading one gap would show up to 35513 FPS |
| lsfg-vk switched live, effects before: base | 2.00 frames shown for every frame the game drew, want 2 |
| lsfg-vk switched live, effects before: off | 1.00 frames shown for every frame the game drew, want 1 |
| lsfg-vk switched live, effects before: on | 2.00 frames shown for every frame the game drew, want 2 |
| lsfg-vk switched live, effects before: three | 3.00 frames shown for every frame the game drew, want 3 |
| lsfg-vk switched live, effects before: file-off | 1.00 frames shown for every frame the game drew, want 1 |
| lsfg-vk switched live, effects after: gaps between presents above lsfg-vk | median 16.671 ms, shortest 1% 16.584 ms, shortest 0.825 ms, so a counter reading one gap would show up to 1212 FPS |
| lsfg-vk switched live, effects after: base | 2.00 frames shown for every frame the game drew, want 2 |
| lsfg-vk switched live, effects after: off | 1.01 frames shown for every frame the game drew, want 1 |
| lsfg-vk switched live, effects after: on | 2.02 frames shown for every frame the game drew, want 2 |
| lsfg-vk switched live, effects after: three | 2.95 frames shown for every frame the game drew, want 3 |
| lsfg-vk switched live, effects after: file-off | 0.99 frames shown for every frame the game drew, want 1 |
| per-frame cost of none in this run | 0.0663 ms per frame from present 7285 on, after Turn off handed the swapchain back, against 0.0642 ms without the layer (present mode 0, xcb) |
| per-frame cost, one effect, wayland | 0.0339 (0.0335..0.0349) ms/frame without, 0.0661 (0.0653..0.0665) with, median of 5 at 1800 minus 600 frames (present mode 0) |
| per-frame cost, one effect, xcb | 0.0708 (0.0702..0.0789) ms/frame without, 0.0842 (0.0839..0.0843) with, median of 5 at 1800 minus 600 frames (present mode 0) |
| per-frame cost of the HUD, wayland | 0.0691 (0.0670..0.0712) ms/frame with the HUD drawing 3 lines (fps,frametime,slowest), 0.0650 (0.0622..0.0671) without it, median of 5 at 1800 minus 600 frames (present mode 0) |
| per-frame cost of the launch notice, wayland | 0.0694 (0.0681..0.0706) ms/frame while it shows, 0.0650 (0.0622..0.0671) without it, median of 5 at 1800 minus 600 frames (present mode 0) |
| memory of one effect image | 1920x1080 4 MB, 3840x2160 16 MB; the layer keeps one per swapchain image for each effect, up to the effect limit |
| per-frame cost of half precision, wayland | 0.8771 (0.8769..0.8841) ms/frame with half precision, 0.8805 (0.8784..0.8844) with full, an effect doing 256 min16float multiply-adds a pixel, median of 5 at 1800 minus 600 frames (present mode 0) |
| per-frame cost of instant ReShade sliders, wayland | 0.0757 (0.0746..0.0760) ms/frame with instant sliders, 0.0745 (0.0742..0.0755) without, uniform_smoke, median of 5 at 1800 minus 600 frames (present mode 0) |
| queue drain vs own fence | over-wait: mean 25.30 ms |

### Apple M1 Max (G13C C0), driver Mesa 26.3.0-devel

| Measured | Result |
| --- | --- |
| per-frame cost, one effect, wayland | 0.1417 (0.1399..0.1469) ms/frame without, 0.2293 (0.1986..0.2533) with, median of 5 at 1800 minus 600 frames (present mode 0) |
| per-frame cost, one effect, xcb | 0.2673 (0.2114..0.3044) ms/frame without, 0.3298 (0.2929..0.3953) with, median of 5 at 1800 minus 600 frames (present mode 0) |
| per-frame cost of the HUD, wayland | 0.2850 (0.2768..0.2973) ms/frame with the HUD drawing 3 lines (fps,frametime,slowest), 0.2166 (0.2000..0.2288) without it, median of 5 at 1800 minus 600 frames (present mode 0) |
| per-frame cost of the launch notice, wayland | 0.3084 (0.2557..0.3300) ms/frame while it shows, 0.2166 (0.2000..0.2288) without it, median of 5 at 1800 minus 600 frames (present mode 0) |
| memory of one effect image | 1920x1080 4 MB, 3840x2160 16 MB; the layer keeps one per swapchain image for each effect, up to the effect limit |
| per-frame cost of half precision, wayland | 0.4168 (0.4106..0.4809) ms/frame with half precision, 0.4264 (0.4109..0.4674) with full, an effect doing 256 min16float multiply-adds a pixel, median of 5 at 1800 minus 600 frames (present mode 0) |
| per-frame cost of instant ReShade sliders, wayland | 0.2420 (0.1955..0.2475) ms/frame with instant sliders, 0.2169 (0.1917..0.2497) without, uniform_smoke, median of 5 at 1800 minus 600 frames (present mode 0) |
| queue drain vs own fence | over-wait: mean 0.01 ms |

<!-- END generated:study -->
