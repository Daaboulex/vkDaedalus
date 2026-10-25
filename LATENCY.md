# How the overlay's Performance card gets its numbers

Every figure on the Performance card is one of three kinds, and the card says which:

- measured: read from a clock, with the formula below;
- derived: computed only from measured figures;
- estimated: a model, stated with its assumption.

The examples in the blocks marked `example` are recomputed by the meson test `latency-stats`, which
runs the layer's own functions on the inputs and compares every printed figure with the text. A
change to a formula that is not made here too fails the build.

## Frame statistics

Source: the time between two of the game's `vkQueuePresentKHR` calls on one swapchain, read from
`std::chrono::steady_clock` when the call enters the layer. This is the interval PresentMon calls
MsBetweenPresents. The layer stamps each interval with the time it ended, and the page shows the
swapchain the overlay draws on: FPS over the intervals that ended in the last second, every other figure
and the plot over the last 10 seconds. No interval is dropped, however long; a swapchain presenting more
than 16384 times in 10 seconds keeps its latest 16384, and the page then gives the shorter time they cover.

For n intervals t1 .. tn, in milliseconds:

- window = t1 + ... + tn, the time the statistics cover;
- average = window / n;
- FPS = 1000 / average, which is n frames over the window, not the mean of per-frame FPS;
- min and max are the shortest and longest interval;
- slowest 1% = the mean of the k longest intervals, k = ceil(n / 100).

```example
frames: 196 x 16.0, 3 x 33.0, 1 x 51.0
count: 200
window: 3286.00 ms
average: 16.43 ms
fps: 60.86
min: 16.00 ms
max: 51.00 ms
slowest 1%: 2 intervals, 42.00 ms
```

## Frame rate with frame generation

The layer times frames where it sits. With its effects before lsfg-vk's frame generation, the
default, it sees only the frames the game makes: their FPS is measured, and the frames shown are
that rate times the multiplier, estimated, since lsfg-vk adds multiplier - 1 frames between two
real ones. With its effects after frame generation it sees every frame shown: that FPS is measured,
and the game's own rate is it divided by the multiplier, estimated. At multiplier 1, or without
lsfg-vk in the game, both are the measured rate.

```example
rates: 60.00 FPS timed, multiplier 3, effects before
game: 60 FPS, measured
shown: 180 FPS, estimated
```

```example
rates after: 180.00 FPS timed, multiplier 3, effects after
game after: 60 FPS, estimated
shown after: 180 FPS, measured
```

The frame time plot reaches up to twice the average or 1.1 times the slowest 1%, whichever is
higher, so one long hitch goes off its top instead of flattening every other frame.

## GPU time

Source: two Vulkan timestamps written into the same command buffer, or into two submissions on the
same queue, which is the only case where the specification lets two timestamps be compared. A GPU's
timestamps count ticks of `timestampPeriod` nanoseconds, and only the low `timestampValidBits` bits
are valid, so the counter wraps:

- ticks = (end - begin) mod 2^validBits;
- time in ms = ticks x timestampPeriod / 1000000.

Two GPUs' timestamps are never compared: each GPU counts its own ticks from its own start.

```example
ticks: 68718976736 to 2000000, 36 valid bits, period 1.0 ns
delta: 2500000 ticks
time: 2.50 ms
```

Each GPU figure is smoothed frame to frame: s = s_prev + 0.15 x (sample - s_prev), the first
sample taken as it is. A sample k frames old weighs 0.15 x 0.85^k, so the frames older than the
last 15 hold under 9% of the figure.

```example
smoothing: previous 2.00 ms, sample 3.00 ms
smoothed: 2.15 ms
older than 15 frames: 8.74%
```

## What vkDaedalus adds to a frame

- Effects on the game's GPU, measured: from the first effect's start stamp to the last effect's
  end stamp. The effects' own intervals are not added up: the specification lets a start stamp be
  written before the effect ahead of it ends, so their sum is no bound.
- Effects on other GPUs and the hand-offs to and from them, measured: from the game's GPU
  finishing its copy out to the same GPU starting its copy back, both stamps on the same queue,
  read back only while the Performance or GPUs card shows them.
- The round trip's parts, with Per-effect GPU timing on, measured: on each other GPU the upload,
  its effects and the download, each between two timestamps on that GPU. Waiting is worked out,
  not measured: the round trip less those parts, the hand-offs between the GPUs and their queues.
- The game's GPU's own copy out and copy back: not measured.
- Effects on the game's GPU after the frame comes back from another GPU: not measured.
- With the effects switched off nothing of vkDaedalus' runs on the frame, and the page says so
  instead of showing the last figures.
- Frame generation, estimated: lsfg-vk shows a frame built between the last two real frames, so it
  holds about one real frame interval, plus its own GPU time, which is not measured. With effects
  before frame generation the layer sees only real frames, and that interval is the average above;
  with effects after it, every generated frame passes through the layer, and the real interval is
  the average times the multiplier. A game at multiplier 1 has no estimate, since lsfg-vk then
  passes frames through.

The page shows frame generation's estimate apart from the measured figures and adds nothing across
the two kinds. Time from input to the screen is
not covered: it needs a clock outside the computer, a camera or a light sensor on the display.

## Checking it yourself

- `meson test -C build latency-stats effect-timing` recomputes every example here.
- The live command's per-frame cost lane runs 1800 frames with a counting layer above the layer,
  which times the same presents on the same clock. The layer must count one interval fewer than
  the presents counted above it, and its summed intervals must match the span from the first
  present to the last within 2 ms plus 0.1% (the median of 3 runs).
