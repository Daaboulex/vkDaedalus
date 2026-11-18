# Performance

What vkDaedalus adds to a frame, measured by the live command. A figure depends on the GPU, its driver and the
rest of the system, so this page holds how each one is taken, not one machine's numbers. To get them for your
own GPUs, save the output of `nix run .#live` to a file, then `nix run .#study -- collect <live log> > figures.tsv`
and `nix run .#study -- show figures.tsv` print one table per GPU.

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
gap is a 60 Hz display's 16.67 ms but the shortest 1% are a small fraction of a millisecond, as the study's
tables show, because lsfg-vk queues a generated frame right behind a real one. A counter that turns one gap into an FPS figure
then shows tens of thousands of FPS while the screen shows 120. Steam's overlay reporting around 10000 FPS
with frame generation fits a counter of that kind sitting below lsfg-vk; its place in the layer order is
not confirmed. vkDaedalus's own Performance card counts the presents over a second and over 10 seconds
instead, and where its present counter runs below frame generation it gives the shortest gap between
the frames shown as a time, not as an FPS figure, as [LATENCY.md](../LATENCY.md) describes.
