# Limitations

## Known limitations

- Only a shader's first technique runs.
- Depth access depends on the game's depth format and layout.
- A game on Wayland needs `vkdaedalus-run` for the overlay to hold its input.
- An X11 game started without `vkdaedalus-run` that holds the pointer, as a Proton game does while it
  captures the mouse, refuses the overlay's grab. It keeps getting the input while the overlay is open,
  and the top bar says so.
- Intel GPU usage is estimated from the clock ratio.
- NVIDIA usage needs `libnvidia-ml.so.1` (`nvidia-utils`).
- Upscaling is absent on purpose: the layer receives a frame already drawn at full size, so there is
  nothing to upscale from. Use gamescope's `-F fsr` or Proton's FSR instead.

## What changes only at the next start

Everything else in the overlay applies while the game runs: effects on and off, adding, removing and
reordering effects, parameters, profiles, Safe Anti-Cheat, moving a row to a GPU the profile listed at
start, keys and the overlay's own settings. Frame generation's key and its On switch, the multiplier,
flow scale, performance mode, colour space and transfer function also apply live: lsfg-vk reloads its
file and the game rebuilds its swapchain.

The controls below are saved at once and marked with the restart icon until a start uses them.

| Control | Why it waits | Live alternative | Proven by |
| --- | --- | --- | --- |
| Effects before or after frame generation | The Vulkan loader fixes the order of the layers when the game creates its instance. vkdaedalus-run sets the order for the next start. | None | live lane ui: "Frame generation: effects after it waits for the next start", "vkdaedalus-run sets the order" |
| A profile switch, rename or new list of games for frame generation | lsfg-vk takes its profile when the game starts and finds it again by name on a reload. | None | live lane ui: "frame generation follows the profile" |
| Force vsync (FIFO), Keep swapchain image count | lsfg-vk reads them when it builds its swapchain, and its reload copies only the multiplier, flow scale, performance mode, colour space and transfer function (lsfg-vk-layer/src/hooks.cpp, Layer::update). | None | live lane ui: "Frame generation: Force vsync (FIFO) waits for the next start" and the same for Keep swapchain image count |
| Half precision, lsfg-vk.dll | lsfg-vk reads them once, when it starts its own Vulkan instance. | None | live lane ui: "Frame generation: Half precision waits for the next start", "Frame generation: Half precision applies at the next start" |
| Frame generation in a game started without lsfg-vk | A layer cannot join an instance that already exists. When lsfg-vk is installed and its DLL is found, vkdaedalus-run adds a 1x profile, so lsfg-vk loads in every game it starts. | Start the game through vkdaedalus-run | live lane ui: "vkdaedalus-run readies lsfg-vk" |
| Depth capture | The layer chooses its image hooks when the game asks for its device functions. | None | live lane ui: "Settings: Depth capture waits for the next start", "Settings: depth capture runs from the next start" |
| A GPU listed after the game's device was made | The four sharing extensions have to be on the game's device when it is created. | List the GPU in the profile before the game starts | live lane offload: "a GPU listed mid-game is used from the next start"; meson test gpu-assignment |
| vkDaedalus off for a game (profile none) | A layer cannot unload itself, so in the run where none is picked its present hook and key poll stay; the game's swapchain goes back to its own images when the game rebuilds it. | The overlay key brings vkDaedalus back in that run | live lane ui: "Effects: none in this run", "per-frame cost of none in this run" |

### Measured live

The live lane framegen runs vkcube under the real lsfg-vk with three effects. With one counter above
lsfg-vk and one below it, the key turns frame generation off and on, then lsfg-vk's file goes to 3x
and back to 1x. Each step must show 2, 1, 2, 3 and 1 frames for every frame the game draws, within
15 percent, with the effects before and after frame generation.

### What could become live

These need a decision before anyone builds them.

| Change | What it costs |
| --- | --- |
| Track depth images always, read them only while depth capture is on | Every image the game creates passes through the layer. |
| Turn on the sharing extensions whenever the machine has a second GPU that can share | They are on every game's device on a multi-GPU machine. |
| lsfg-vk copies Force vsync and Keep swapchain image count on a reload | A change in lsfg-vk, not here: two fields in Layer::update. |
| Load the layer at both places around lsfg-vk, one of them active | Not proven to work with the loader. |
