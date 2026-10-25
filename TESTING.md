# Testing

## Automated, run these

The flake checks run on every commit and need no GPU. The live command runs on
every Vulkan GPU the machine has, each inside a private session it starts for
that GPU: a headless weston rendering with its Vulkan renderer on the same
driver as the application under test, and an Xwayland nested in it. On a machine
with more than one physical GPU, whatever the vendors, each GPU is pinned for
its own run with the loader's `VK_LOADER_DEVICE_ID_FILTER`, for the application
and the compositor alike. Before any lane a GPU fails unless Vulkan lists
exactly that GPU, weston reports rendering on it and Xwayland has GPU access, so
no run presents across GPUs unnoticed. Each GPU must also load the layer from
the build under test, not an installed copy of it. Two identical cards cannot be
told apart by their IDs and are tested once. A GPU the kernel shows as a render
node that no Vulkan driver opens, such as an NVIDIA card whose driver and kernel
module disagree after an update, fails the run while the other GPUs are still
tested, unless driver manifests are named on the command line. The live command
also starts from an empty environment, keeping only `PATH`, `HOME`, `TMPDIR`,
the XDG home folders and its own `VKB_` paths, and every game it runs gets only
the variables that run names, so no session variable, such as
`MESA_VK_DEVICE_SELECT`, a global `ENABLE_VKDAEDALUS=1`, `LSFGVK_CONFIG` or a
locale, reaches a run. Nothing appears on the
desktop and the user's display is never touched, so it runs the same from a
desktop or over SSH.

| What | Command | Proves |
| --- | --- | --- |
| Build checks | `nix flake check` | Every invariant the layer relies on, each ablation-verified; includes the clang build, the wayland-1.20 header compile, the cache round-trip, and on x86-64 the 32-bit build |
| Unit tests | `meson test -C build` | Runs on any build, nix or plain meson: every shader fixture compiles and survives the cache round-trip, a cache entry with one byte changed on disk is recompiled rather than trusted, a cached entry with no technique or no SPIR-V, a uniform outside its block, or a pass, sampler, storage or render target naming a shader or texture it does not have is refused, a cleared cache compiles again, Safe Anti-Cheat stops depth capture without erasing the saved setting and is on for any game whose own file does not say safeAntiCheat = false, whichever profile it runs, an added effect compiles in the background while the overlay keeps drawing, a different build of the program never reads another build's entries, a header added earlier in the include search order or appearing where `#if exists` looked recompiles the effect, a Wine or Proton game is named by its .exe however Wine rewrote its arguments, vkdaedalus-run names a game's command line the way the layer names its process and reads the effects-after-frame-generation choice from the profile the layer would run, a new Wine game runs the profile the shared Wine name runs, with Safe Anti-Cheat on, profiles from before they were shared move to profiles/ once, backed up first and named so no two programs' profiles collide, each program running what it ran with that profile's Safe Anti-Cheat, a second start changing nothing, a backup that cannot be written changing nothing, and a program starting while another moves them waiting for it, a renamed profile takes every game running it along and a deleted one sends them to default, a game's folder is found for Steam, Unreal and plain layouts and an anti-cheat's files in it are matched against SteamDB's FileDetectionRuleSets, the overlay's item map lists the items it drew and keeps the name given to one ImGui leaves unlabelled, a card's footer sits below a body taller than the card, never over it, the layer asks the device for a queue of its own only in a graphics family with room to spare, the ReShade key map is correct, an effect lists only its own preprocessor definitions, never one the layer sets from the swapchain, and offers the RESHADE_ settings it reads as global ones, a global definition in vkdaedalus.conf reaches every profile and a profile's own wins, a new profile whose name cannot be a file vkDaedalus reads back (a slash, an @, an =, a space at either end, . or ..) is refused with the reason and leaves no file, as is one that cannot be written, an empty or malformed global definition is refused, a parameter its definition hides keeps its saved value through a save while one reset to its default does not, a GPU's limits refuse a texture larger than it makes and a thread group or a dispatch larger than it runs, and hold a MipLODBias at its limit, an integer format is sampled and mipped from the nearest texel and a float format the GPU cannot filter goes unfiltered while one it cannot blit makes no mips, a GPU is named by vendor:device in hex with a PCI address for identical cards, a row's GPU number applies to it and the rows after it, a malformed gpus entry keeps its place and a row past the list or a number outside 1 to 64 is reported, a profile's GPU list and a row's GPU number survive an overlay save, an edited GPU list replaces the saved one and a row put back on the game's GPU loses its saved number, the hand-off submits the head and every segment of the chain on its GPU's queue in order with their waits and fences, a segment back on GPU 1 mid-chain with no fence of its own, and on any failing step leaves the frame to the pass-through with the right wait, closes every fd an import did not take and puts a reset fence back signalled, lsfg-vk's own file, read as lsfg-vk reads it, keeps every byte but the values the overlay changes, a file or a value lsfg-vk would refuse is refused with lsfg-vk's reason, the profile lsfg-vk uses in a game is the one changed, and lsfg-vk.dll is found in every Steam library Steam lists, a Wayland driver loaded the way Wine loads winewayland.so has the listeners it adds at its start wrapped by the input hook, which keeps presses, motion and the wheel from the game only while asked and lets releases through, one mouse sample serves a whole frame, and the frame statistics and every worked example in LATENCY.md come out as the text says |
| Shader corpus | `nix flake check` | Every shader in the packs of ReShade's official index, pinned in `test/shader-corpus.lock.json`, still compiles through the layer's own compile and acceptance rule (so a shader the layer refuses, one with no technique among them, never counts as compatible), the SPIR-V it emits is valid, and no verdict drifted from `test/shader-corpus-baseline.txt` (checks.shader-corpus-matches-the-baseline) |
| Live, on every GPU | `nix run .#live` | Per real GPU, with the Khronos validation layer (synchronization validation on) under the layer and its errors and warnings counted only when absent from a run without it: a real application loads the layer, applies a chain of the CAS, SMAA and LUT built-ins and five compiled ReShade effects (compute and per-frame uniforms included) and runs warm from the disk cache, drawing no overlay frame and taking no GPU timing nobody asked for; nothing is written outside the XDG directories; the layer survives a mock frame-generation layer that copies lsfg-vk's present behaviour, above and below it; it presents under Wayland, X11 xcb and xlib, and gamescope where gamescope runs; its configured overlay key, pressed over XTEST, opens the overlay and the overlay draws under validation; where the GPU's graphics family has three queues, a game that submits on one from another thread while it presents on the other gets the layer's submits on the queue it presents on, validation clean; the overlay draws on two swapchains of one device, an 8-bit UNORM and an sRGB one presented in turn, validation clean; the open overlay holds the pointer, and moving the input focus away from the game closes it and lets the pointer go, and a game started with vkdaedalus-run that holds its own pointer grab, as Wine does, gets no key, button or motion while the overlay is open and its keys again once it closes; the effect toggle key turns the effects off and on, and the reload key (named in lower case in settings.conf, as any key name may be), an edit to the config the layer watches, an edit to its `effects =` line that adds an effect and emptying that config each rebuild the chain exactly once, the added effect built and the image pool grown for it, the empty file keeping the previous settings, and the reloads add no shader cache entry; a disabled effect stays out of the chain, an effect whose saved file is gone runs from its include folder and the log names both, a chain longer than the effect limit runs only the effects within it and names the rest left out, a 1 MiB VRAM limit is reported once and refuses nothing, an overlay left open with input blocked releases it when the game exits, a program set to none runs no surface, swapchain, present or depth hook, a home in the layout from before shared profiles moves to profiles/ at the first start with a backup and runs what it ran, depth capture feeds a DEPTH-semantic effect, a new game with depth capture on gets Safe Anti-Cheat by default, captures no depth image and leaves a depth-reading effect out of the chain without the overlay ever opening, a game whose folder holds an anti-cheat's files keeps Safe Anti-Cheat on although its profile turns it off, two effects share one generated texture while an incompatible redeclaration is refused and a storage writer gets its own copy, a device made and destroyed with the application's own allocation callbacks, with a transient depth attachment made under depth capture, stays validation clean, has a depth image bound with vkBindImageMemory2 captured like one bound with vkBindImageMemory, and gets the application's VkPhysicalDeviceFeatures2 back as it was given, an application's own mutable-format swapchain with a one- or two-format list stays validation clean and gets its list back unchanged, a swapchain on a queue family without graphics (where the GPU has one) is left alone, textures declared in the packed, 10-bit and integer formats with mip chains (one too long) and 1D and 3D textures sampled and stored as declared, with integer mip levels and an integer sampler asking for linear filtering taken to the nearest texel, reach Vulkan valid while an unknown format and an image file for a 4x4x4 texture are refused and a uniform asking for a source the layer does not provide is reported, a texture larger than the GPU makes, a compute thread group larger than it runs and more groups than it dispatches each leave that effect out on that GPU with the reason while the rest of the chain runs, an inverted LOD range is refused, a MipLODBias beyond the GPU's limit is held there and reported, and a resize with the overlay open keeps it drawing, and on two swapchains of one device destroying a depth image no effect reads re-records nothing while destroying the one the effects read re-records them; the overlay is driven with the mouse through its item map (`VKDAEDALUS_UI_MAP`): the game's environment holds the variables its run names and no other, the overlay opens over the whole frame though imgui.ini holds a window saved 400 px wide, every profile is listed with the number of games that run it and none greyed, and a profile file still saying safeAntiCheat = false runs with Safe Anti-Cheat on, an effect the GPU cannot run is listed as left out on this GPU, the Safe Anti-Cheat checkbox saves both ways into the game's own file and never into the profile, an expanded effect row is removed with no ImGui error, added effects join after their background compile, with a global definition set and nothing compiled under the layer lock, and are saved with no other edit, a definition switched on in an effect's Definitions and applied brings up the parameter it gates, the footer saying Waits for Apply until it is applied, with an lsfg-vk the game's loader lists and an lsfg-vk file already holding another game's profile, the Frame generation switch leaves the file as it was while lsfg-vk.dll is nowhere lsfg-vk looks, then adds the lsfg-vk profile of the profile the game runs, listing the game, in the swapchain's colour space, keeps every byte the file held before through every change, and switched off keeps the profile at multiplier 1, and its "Effects after frame generation" box saves into the profile the game runs, a new profile takes the game out of the old profile's lsfg-vk list, vkdaedalus-run sets VKDAEDALUS_AFTER_LSFGVK=1 for a game whose profile asks for it and clears it for one without, though that game was started with it set, and with lsfg-vk installed lists a game with no lsfg-vk profile in the one of its vkDaedalus profile at multiplier 1 before it starts but adds none while lsfg-vk.dll is missing, `vkdaedalus-run --profile` brings a game set to none back and `vkdaedalus-profile` prints and sets a game's profile, refusing one that does not exist, vkdaedalus-run puts its two order layers before every other implicit layer folder, and with lsfg-vk named they put it where the profile asks, the lsfg-vk named with `--lsfg-vk` loads and reads the file the overlay wrote with no configuration error, the open overlay keeps the game's input with no setting asked for, a new profile is made and switched to, then deleted and switched away from, no compile runs under the layer lock across those switches, the added effects and the definition edit, the shader test completes while the game draws and the Performance card opens, the GPUs page draws a usage meter for every GPU whose driver reports how busy it is, and a video memory meter for every GPU that reports its memory, and a run with VKDAEDALUS_CONFIG_FILE set offers no profile menu, whose choices it could not keep; separate runs then drive every other control a user can reach and read its effect back from the file or log line it changes: the Settings page (each switch, the log level menu held while VKDAEDALUS_LOG_LEVEL sets the level, the limit, the auto-apply delay, the three key bindings, none of which runs on the press that binds it, and a key another action uses refused, and Per-effect GPU timing on the Performance card), the debug window (its trees, the Frame path and Paths tabs, filters, search, Clear, and Export writing one report to the download folder), the HUD drawn once the overlay closes, a colour scheme picked and saved, a setting changed from its default marked and set back by its mark, the Shaders page (Browse, Rescan all, which names a pack folder it may not read and goes on, each folder's X, a pack folder's X taking the folders found in it along, Clear compiled effects, the test's result lists), the Effects page's other controls (its definitions for every effect added, refused, reset and removed, a row's ... button moving it down and back, the effects switch, a row's checkbox, saved though the overlay closes at once, Reset to defaults, Insert effects here, Half precision, drag to reorder, the none profile asking first, then turning vkDaedalus off in this run so the game rebuilds its swapchain and keeps its own images, and the overlay key bringing it back, a profile two games run renamed with both games moving along, a profile picked with Apply automatically off building its chain without Apply, both popups' Cancel, Add effects' x, Cancel, keys typed while the overlay is hidden kept out of its search and its greyed entries at the limit and for a depth effect) and a parameter editor of every type with its default mark, and a pack's parameters: a category, a closed one opening, a hidden parameter and the author's text taking no row, the text behind an i, a radio's choices, a value with units and a colour typed into its picker, Frame generation's multiplier, flow scale (typed values beyond either end saved as that end), performance mode and colour space, and the GPUs page's Up, Down and Remove; a shipped anti-cheat shows the Safe Anti-Cheat checkbox greyed; the screenshot layer under the layer reads a presented frame, which must carry a solid-fill effect's colour, also behind an effect that never writes the back buffer, and must show the third of three textures an effect samples while skipping the second, and a compute pass must read what an earlier pass wrote to the back buffer, and a pass naming RenderTarget1 but no RenderTarget0 must draw to the back buffer as ReShade does, and a global definition in vkdaedalus.conf must reach the compile and pick the colour an effect presents while an empty one is ignored and the effect keeps its own default, and a parameter behind a saved definition must present its saved value; with the overlay open over a solid fill, the centre of the frame shows the overlay rather than the fill; upstream-style casSharpness, dlsSharpness and dlsDenoise in vkdaedalus.conf must change the presented frame's variance the way they should; a render target written with GenerateMipMaps = false must keep its lower levels untouched, and the solid-fill, texture, compute, mip and render-target frame checks each run again on the warm shader cache their first run wrote and must read the same colour; a timer uniform with an initializer must change between frames 30 and 60; with the layer's mapped memory poisoned by the fault layer, a uniform without an initializer must present its configured value and a uniform nothing writes must read zero; a fault-injection layer under the layer fails each Vulkan call the layer makes, one call at a time (a failed uniform-buffer map must put a pass-through in that ReShade effect's place), and each time the application runs on with validation clean and the failure reported, and the overlay key never grabs input for an overlay that cannot draw, and a single vkQueueSubmit or vkWaitForFences the layer makes failing once (the first frame's effect submit, the open overlay's submit, the first texture upload's wait and the first frame's wait, each reported as that call) leaves the application running all its frames with validation clean and the failure reported, and a reload whose first command buffer allocation fails still puts every presented frame through the layer, and on a device that does not offer VK_KHR_image_format_list, or one that does not offer VK_KHR_swapchain_mutable_format so the layer takes its transfer path (each hidden by the fault layer, which also refuses a device asking for it), every frame still goes through the layer with validation clean, and with linear filtering of a float format hidden by the fault layer, an effect that samples it and makes its mip levels runs them unfiltered on that GPU and says so; with every driver's GPUs in the instance and no device filter, the game's device list, its order and its default pick are the same with the layer as without, before and after it presents, also with a profile that assigns a row to GPU 2, and the game's device enables the sharing extensions only for such a profile or names what it lacks; a GPU 2 the instance lacks is refused once, and where the machine has a second GPU the layer makes a device on it through the loader's callback, once, and closes it with validation clean, and a chain whose last row runs on that GPU presents the same frame 120 as on the game's GPU alone (solid_fill then invert_input, 191 127 64), validation clean, one failed wait for GPU 2's previous frame, injected by the fault layer on that device alone, moves the rows to GPU 1 until a reload instead of rebuilding onto GPU 2 every frame, the row menu moves that row to GPU 1 and back to GPU 2 while the game runs, each choice rebuilding the chain, a chain that goes to GPU 2 and comes back to GPU 1 mid-chain and, where the machine has a third GPU, one crossing GPU 2 then GPU 3, each present the frame their rows make (64 128 191 after two inversions), and with a row on GPU 3, Up on the GPUs page keeps that row on the same card as GPU 2 and Remove then saves it on GPU 1; and the per-frame cost and queue-wait figures are printed, the layer counting one present interval fewer than the presents a counting layer above it times on the same clock and summing them to within 2 ms plus 0.1% of that layer's span, and the cost of the HUD, of the launch notice while it shows and of none in this run are measured. Software drivers and drivers with no device are listed and skipped, a machine with no real GPU fails, and so does a GPU on which no lane recorded a result |

Pass driver manifests as arguments to test only those, for example
`nix run .#live -- /run/opengl-driver/share/vulkan/icd.d/radeon_icd.x86_64.json`.

On a machine with more than one GPU or driver, `nix run .#live -- --parallel`
runs the lanes of every driver and GPU it would test at the same time (two
drivers for one GPU, like two Mesa installs, share that GPU), then the
per-frame cost one at a time, so its figures are not shared with another run's
load. Each run keeps its own compositor, runtime folder, X display (picked by
Xwayland itself, so two sessions cannot take the same one), work folder and
processes: a lane reads only the game process carrying its own HOME. The run
fails if a file appears at the top of the work folder outside the runs' own
folders. The default stays one run after another. In either mode, a failing
command that ends the run, one GPU's lanes or a step running beside the game
names itself and its status, so the cases it cut short are not mistaken for
passes.

`--gallery <folder>` keeps what the overlay showed. On the first GPU the UI
and offload lanes save each overlay state they reach as a PNG cut to the
overlay: the Effects card, the profile menu, an applied definition, a row's
menu, Add effects with a search typed, the new, rename and delete profile popups,
the shader test, an open effect of every parameter type, a pack's grouped
parameters, Settings, the HUD menu, the Frame path tab, Performance, the GPU readings, Settings in the
compact layout of a 1024 by 768 window, Frame generation switched on, the GPU
menu and the GPUs card, then Performance and the GPUs card again with
Per-effect GPU timing on and a row on GPU 2. Each run replaces the folder whole
and names any state it did not reach.

Every state those lanes reach is checked, with or without `--gallery`: a page
that scrolls, a control off the kit heights or outside its window, two
neighbours on one line almost aligned or closer than 24 and neither 8 nor 16
apart, a menu over the item that opened it and a card with no footer each fail
the state. The Performance, GPUs and Frame generation cards are also drawn at
the sizes of 1080p at 125% and 1440p at 150% (84% and 75% of the 1280x720
window), where a body that scrolls fails, with one GPU, a chain on two and a
chain on three; at 1280x720 the plot, the timing switch and every GPU row must
stay in view.

`--lsfg-vk <manifest>` names an installed lsfg-vk by its layer manifest,
VkLayer_LSFGVK_frame_generation.json. The UI lane then runs it on the
lsfg-vk config the overlay wrote and fails when it refuses the file, which
checks the files against lsfg-vk itself, not only its documented keys.
Without it that case is skipped.

`--lsfg-dll <dll>` names Lossless Scaling's lsfg-vk.dll as well. With both,
the frame generation lane runs vkcube under the real lsfg-vk at 2x with the
layer above it and below it, and the offload lane runs it with cas on GPU 1
and smaa on GPU 2 in both orders: every run must make lsfg-vk's device,
swapchain and pipeline with no lsfg-vk warning, present once per game frame
with the layer above and twice with it below, and add no validation report to
a run of lsfg-vk alone. The frame generation lane then switches lsfg-vk while
vkcube runs cas, smaa and uniform_smoke, with one counting layer above lsfg-vk
and one below it: the frame generation key off and on, then lsfg-vk's file at
3x and at 1x. Each step must show 2, 1, 2, 3 and 1 frames for every frame the
game draws, within 15%, in both orders, and each step is listed as a MEAS line.

`--layer <manifest>`, as often as wanted, names another Vulkan layer a game
may run beside vkDaedalus, such as MangoHud or Steam's overlay. The layer lane
runs each alone first and leaves out one that fails on its own, then runs
vkcube with cas, smaa and uniform_smoke under vkDaedalus and every set of up
to three of them, lsfg-vk included when named, in every order on Wayland, the
pairs on X11 too, and all of them with vkDaedalus and lsfg-vk at every place.
A counting layer at the bottom gives what reached the driver; every run must
make the three effects, draw all its frames, present as lsfg-vk's place
implies, have vkDaedalus name that place, and log frame rates within 15% of
what was counted; on X11 the overlay must open and draw. Each run is listed as
a MEAS line, the data behind the verdict. Each X11 run starts on a fresh
Xwayland: one server for every lane ran out of Mesa's 32-bit address space on
the RX 9070 XT after 187 X11 layer runs and the lanes before them, and a server
that stops answering fails the case at once, with its log line.

A control read only when a game starts is tested over two starts. The UI lane
changes depth capture, the effects order and half precision, and each restart
mark must show the change waiting. The next start must show depth capture and
half precision applied, depth capture tracking vkcube's depth image, and the
effects order still waiting, since only vkdaedalus-run applies it; Force vsync
and Keep swapchain image count, changed there, must show theirs waiting. The
offload lane lists GPU 2 in a running game's profile: the layer must say it
shares nothing until a restart, and the next start must run the row on GPU 2.
[What changes only at the next start](docs/limitations.md) lists these
controls.

To attach a run to a bug report, keep its logs and save what it prints:

```bash
nix run .#live -- --keep-logs >vkdaedalus-live.txt 2>&1
```

The file holds the PASS or FAIL table of every GPU and ends with the folder the
logs were kept in (`logs kept in /tmp/vkdaedalus-live.XXXXXX`). Pack that folder
with `tar -czf vkdaedalus-live-logs.tar.gz -C /tmp/vkdaedalus-live.XXXXXX .` and
attach both files. Without `--keep-logs` the folder is kept only when a lane
failed.

`nix run .#sync-corpus` pins every pack in the index to its current revision and
hash in `test/shader-corpus.lock.json`; it needs network access. The checks
fetch their pinned, hash-checked sources once and then run offline. A pin change
that moves a verdict fails the corpus check until the baseline is recorded
again, with `cat "$(nix build --no-link --print-out-paths
.#shader-corpus-verdicts)" > test/shader-corpus-baseline.txt` and then
`scripts/shader-support-table.sh`. Record only when every changed verdict is
explained in the commit.

## Where it was tested

The last full test of this release, on 2026-10-25, ran on NixOS with these GPUs:

| GPU | Vulkan driver | What ran |
| --- | --- | --- |
| AMD Radeon RX 9070 XT | Mesa 26.3.0-devel, RADV | `nix run .#live`, every lane, with lsfg-vk 2 and Lossless Scaling's lsfg-vk.dll |
| NVIDIA GeForce GTX 1660 SUPER | NVIDIA 595.104.02 | the same run, the same x86-64 machine |
| AMD Radeon integrated graphics (Raphael) | Mesa 26.3.0-devel, RADV | the same run, the same x86-64 machine |
| Apple M1 Max (G13C C0) | Mesa 26.3.0-devel, Honeykrisp | the UI and surfaces lanes, on aarch64 |

No other GPU, driver or distribution ran these lanes for this release.

## Other distributions

`nix run .#live` starts only nix-built programs, and those load GPU drivers
only from NixOS's `/run/opengl-driver`; elsewhere it stops and says so. On any
other distribution the same lanes run on the host's own programs: build the
layer with plain meson, then point `scripts/live-host.sh` at the build folder.

```bash
meson setup build && ninja -C build
scripts/live-host.sh build --keep-logs
```

It needs a C compiler with the Vulkan, xcb and X11 development files, vkcube
and vulkaninfo, weston 14 or newer (for its Vulkan renderer), Xwayland,
xdotool and gawk, the Khronos validation layer and LunarG's screenshot layer,
and names whatever is missing. It builds the test probes and layers with the
host compiler, puts them and the layer under test in a folder under
`~/.cache` that it removes at the end, and runs the same script the nix
command runs, so every verdict compares a run with the layer against a run
without it on the same host tools.
