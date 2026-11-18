# Design

The decisions behind the code and the facts about outside systems they rest on. What the code, a
check's message or a test already says is not repeated here.

## Layer manifest

- The layer's API version has one source, `meson.build`. The manifest and the `specVersion` the layer
  reports are generated from it and never written by hand.
- The API version is cosmetic on Vulkan loaders from 1.3.228 on: they enable an implicit layer
  whatever version it names.
- The manifest uses the first file format version that has `library_arch`, which lets the loader tell
  the 64-bit build from the 32-bit one.

## ReShade level

- `__RESHADE__` names the ReShade runtime whose behaviour the layer keeps, not the version of the
  compiler it builds.
- The compiler follows ReShade's main branch.
- The level rises only after every FX-relevant release note up to it is checked against the layer,
  item by item.

## Shader cache

- The cache key starts with a hash of the binary the cache code runs from. Any rebuild compiles once
  and reads no other build's entries, and nothing is bumped by hand.
- A hand-kept schema version is not used: one forgotten bump made every warm launch drop a field the
  renderer reads.

## Third-party sources

- lsfg-vk's source is under a Creative Commons licence that allows no derivatives and no commercial
  use. One check compiles it unmodified into a test probe. It is never linked into the layer and
  never shipped.
- MAKO's source, under the GPL, is pinned at a release only so that one check compiles MAKO's own
  file reader and status writer unmodified into a test probe. It is never linked into the layer and
  never shipped.
- The ReShade compiler carries the patches in `subprojects/packagefiles/reshade/`, applied by the Nix
  build and the meson build alike. The file list is the record.
- A ReShade patch that upstream absorbs stops applying and names itself.
- ImGui carries no patch, and none of its backends is built: the overlay draws through the layer's
  own renderer.
- The anti-cheat file rules are embedded at build time: a data file beside the layer is invisible
  inside Steam's container.
- The SMAA and FXAA shaders with their lookup textures and `stb_image_dds.h` are vendored in `src/`:
  their upstreams do not move.
- The Barlow fonts are vendored: an archive of their upstream repository is too large to fetch on
  every lock refresh.

## Programs and config files

- A program is named by the file name of its executable.
- A Wine or Proton game is named by the `.exe` in its arguments, searched from the first argument
  on: Wine rewrites the arguments to the game's Windows path.
- The kernel's process name is not used: the kernel cuts it to 15 characters.
- One function, `programBypassed`, decides whether the layer stays out of a program. The dispatch,
  the config load and the instance and device creation all read it, so a bypassed program gets no
  hook and its create info passes on untouched.
- `shader_manager.conf` keeps each value whole, with no quoting and no comment stripping: a path can
  hold `#`. A value written there is never quoted.

## Device creation and handles

- When creating the device with a queue of the layer's own fails and the driver made no device, the
  layer creates it again with the game's queues, on the chain link as it stood before the first
  attempt: every layer below moves the link on in place.
- When a layer below fails a device the driver did make, the layer returns the error and does not
  ask again. The loader counts that device until the game destroys it, so a second ask would count
  one device twice.
- Every command buffer the layer allocates, and its own queue, carries the device's loader dispatch.
  A handle without it is invisible to drivers and aborts the game once a layer below looks it up.

## Swapchain

- The layer adds usage bits to the game's swapchain request and never replaces them: a frame
  generation layer below needs the transfer bits to stay.
- The pool of images the game receives grows only at its end: the game holds the first handles for
  the swapchain's life.
- A swapchain made after the first at a new size or format runs a plain copy until no other
  swapchain has been made for 200 ms, then that swapchain alone gets its effects back. One that
  replaces a swapchain of the same size and format, as a frame generation layer's live change does,
  gets its effects at once. A swapchain named as the old one of a new
  swapchain is retired and never rebuilt: the driver may free its images while it still exists, and
  a frame generation layer above keeps it until its last present is retired.

## Waits

- Work submitted without a fence, because its fence could not be made, is waited out by draining the
  queue it went to. When that is a queue of the game, the drain can still race another game thread.

## Overlay renderer

- Each swapchain owns its overlay draws, and the layer's own renderer draws them. MangoHud and the
  Mesa overlay have the same shape.
- A swapchain's draws are sized from its own image count. With a frame generation layer raising the
  image count of each new swapchain, draws sized for an earlier swapchain left the overlay missing on
  the new image index.
- ImGui keeps one current context per process, so each device's overlay has a context of its own and
  makes it current before every frame.
- No live lane runs two devices with the overlay open. The source check is all that holds the
  context rule.

## Overlay status

- Every status the overlay states is one badge of the kit: a fill of its tone over the surface, an
  icon in the tone and short text. Green runs now, grey is off, amber waits for something, red is a
  fault and the palette's info colour informs. The text is a lighter shade of the tone: the tone's
  own colour on its own fill read below 4.5:1 on several palettes.
- A setting read only when a game starts says so in its tooltip, and its row shows an amber Next
  launch badge only while the saved value differs from the one the run started with. A row keeps no
  empty room for it.

## Safe Anti-Cheat

- It is on by default, so that no game causes a ban by accident.
- It has one value. That value is read at start, on a config switch and on a file change, and never
  on a plain rebuild: the tile's own rebuild has to keep the click.
- It masks depth capture and does not overwrite it, so the stored setting survives.
- An effect it refuses is left out and marked. The refusal is never saved and never disables the
  effect.
- It does not hide the layer and never says it does.
- The search of the game's folder for anti-cheat files runs on a thread of its own, and only a reader
  that needs the answer waits for it. Matched in place, a folder at the entry limit held the start
  back by about a second, nearly all of it in `std::regex`.

## Input

- X11 support is always compiled in. X11 is not going away, and the layer supports old games.
- The mouse is read once per frame, in the present path. A read drains the wheel, so two readers in
  one frame split its scrolling. On X11 it also halves the round trips to the server.

## Input hook

- The hook is a preloaded library: a game's listeners have to be wrapped from the start of the
  process.
- Each function the hook takes over calls the next definition after the hook in the dynamic
  loader's order, so another library that hooks the same function before it, as Steam's overlay
  does with `XGrabPointer`, is passed on rather than called again. Only when no later definition
  exists, as when Wine loads libwayland-client or libX11 in a local scope, does it call the one
  inside that library's own file. Steam's overlay takes over `dlsym` and answers a lookup of a
  function it hooks (`XGrabPointer`, `XUngrabPointer`, `XCloseDisplay`) with its own, through
  libX11's handle too; only `RTLD_NEXT` gets the next definition. A lookup through the library's
  handle found the overlay, which called the hook back and locked it. The overlay needs libGL, which
  loads libX11 in the global scope, so `RTLD_NEXT` finds libX11 wherever the overlay runs.
- The layer finds the hook through one exported table. The hook never calls into the layer, which
  may unload.
- Wine's `SetCursorPos` warps the pointer while Wine holds a pointer grab, which no event filter
  sees. So the hook answers the pointer query and takes the warp itself.
- The pointer is held where it was when the overlay opened, read by the layer on its own display:
  Wine reads the pointer late.
- A game with a locked camera clips the cursor to one pixel, which Wine holds as a pointer grab
  confined to that pixel, and the overlay reads that same pointer. So while the overlay is open the
  hook lets go of the game's pointer grab and keeps any grab the game asks for meanwhile, then gives
  it back before the game reads input again. It gives it back through XCB, so a grab the X server
  refuses reaches the hook's log rather than the game's X error handler. A grab is kept per X
  connection and dropped when that connection closes. `XCloseDisplay` is not taken over: with it
  taken over, Proton's start under NVIDIA's GLX library called it without end and never went on.
- Pointer crossing events are held too: Wine turns an EnterNotify into a mouse move.
- A game that asks the X server for detectable autorepeat gets a held key's repeats as presses with
  no release between. So a repeat dropped while the overlay is open never holds back the release of
  a key the game saw go down, or that key sticks down in the game.
- Proton's Wine, and builds that carry its patches such as wine-cachyos, read raw input for the whole
  prefix in explorer.exe's desktop thread and send it to the game through wineserver. Upstream Wine
  reads it in the game's own thread while the cursor is clipped. So the hooks of one prefix share the
  open overlay through a file named by the prefix's device and inode, as wineserver names its own
  folder, and every other process of the prefix holds raw input while it is set. Window events still
  go to the window's own process, so only raw input is held there.
- A game that ends or crashes with its overlay open releases the prefix's raw input: the holder is a
  process id with its start time, which a later process given the same id does not match, and the
  other processes check it at most every 100 ms and clear a holder that is gone.

## Second GPU

- Every shared buffer ends in padding and every copy is rounded up to a whole block: RADV's compute
  copy (Mesa at a51a418) reads past the end of its source, which faulted the GPU.
- When a hand-off fails after the head handed the game's frame on, no semaphore of the head is left
  for the frame without effects to wait, so it waits for the game's queue to drain first.
- Rows that run on GPU 1 after another GPU write their last row straight into the swapchain image
  where the device can view it in both formats, as the rows before any crossing do.

## Frame generation

- lsfg-vk stays a layer of its own. Its licence allows no derivatives, so its core is not linked in.
- MAKO stays a layer of its own too, and everything it exposes is driven from the overlay, as
  lsfg-vk is. Its Qt app, `mako-cli` and the Decky plugin stay optional.
- The overlay edits the one file each layer and its own programs read, by the layer's own rules. One
  file has two editors, and no second copy of the settings exists.
- Frame generation works the first time, with nothing configured by hand.
- Installed means the game's loader lists the layer. The loader also lists a manifest that is
  disabled or whose library is missing, and the game still runs, so loaded is read from the
  libraries the process has mapped.
- One frame generation layer per game: two in one chain would each generate frames from the other's.
  The choice lives in the vkDaedalus profile, and `vkdaedalus-run` loads that layer and keeps the
  other out with its `DISABLE_` variable.
- A new profile runs lsfg-vk when both are installed, so nothing changes for anyone until they pick
  MAKO: launch options that set `ENABLE_MAKO=1` without `DISABLE_MAKO` still get MAKO, as they did
  before the choice existed. A layer the profile names wins over them.
- Exactly one order layer is on in a game started with `vkdaedalus-run`. The two that put
  vkDaedalus first are keyed on the layer `vkdaedalus-run` names, never on a `DISABLE_` variable:
  the launch options can set those too, and `DISABLE_MAKO` from them once turned on the lsfg-vk order
  layer beside MAKO's.
- A `DISABLE_` variable does not say who set it, so `vkdaedalus-run` records the ones the game was
  started with before it sets its own. Only those are named as the reason a layer is out; one it set
  for the other layer leaves the profile's new layer to the next launch. A run started by another
  run, as a game by its launcher, drops the `DISABLE_` variables and the `ENABLE_MAKO=1` the outer run
  set and did not record: they chose the layer of the launcher's profile, not of the game's. An
  `ENABLE_MAKO=1` the outer run's caller set is recorded too and counts for the game.
- `mako-launch` is not used: it replaces `VK_IMPLICIT_LAYER_PATH`, which drops vkDaedalus.
  `vkdaedalus-run` sets the one variable of it MAKO needs, `ENABLE_MAKO=1`.
- MAKO's Ultra performance holds for a whole process: it locks the flow scale, turns performance mode
  on, raises MAKO FG mode Quality and Balanced to Performance and, while Scaling is on, scales with
  MAKO Scaler whatever the method says. So the cards grey those by the Ultra performance the running
  process started with, and by the saved one while MAKO does not run the profile.
- MAKO's own layer never reads its Gamescope VRR choice: `mako-vrr-lease` applies it, a helper only
  MAKO's launchers start. So the card shows the choice greyed and starts no helper.
- MAKO follows a display refresh, compares its refresh threshold and sizes a supersampled surface
  only under gamescope, and with Display refresh on there it aims at the saved target only while
  gamescope reports no refresh. So the card greys those three outside gamescope and Target while
  Display refresh is on, and takes the target MAKO applies from its status file.
- The frames shown are timed by a counter layer below frame generation, since the layer above it
  sees only the game's frames. It stamps presents per device, not per swapchain: a frame generation
  layer may hand the game a swapchain of its own, and the device is the handle both sides share.
- The card asks for the layer once. lsfg-vk, MAKO with Lossless Scaling's model and MAKO with its own
  are one choice of three: each needs something else installed, and all three take effect at the next
  launch. MAKO's own editors call Lossless Scaling's model LSFG, so the entry is MAKO LSFG.
- Whether MAKO generates frames on the GPU its file names depends on the pair of GPUs and on the
  model, so the card offers the GPU and shows what took. MAKO LSFG gets a GPU picker that writes
  MAKO's `gpu` key in MAKO's own vendor and device form, or removes it for the game's GPU. MAKO FG,
  which started on no GPU but the game's, gets the picker greyed and an offer to clear a key that
  names another GPU. The card names the GPU in a row of its own: read from a tooltip alone, the card
  looked as if it lacked a GPU choice.
- Where MAKO generates frames is read from its status file, never from its settings: the GPU its
  applied profile names and the error it wrote. That GPU is matched by MAKO's own rule, so a name
  two GPUs share is claimed as either. The layer also reads the file without the overlay, at a low
  rate, and logs the claim when it changes, so every run records where its frames were generated.
- Every GPU of the instance has a number, a listed one its place in the profile's list and the rest
  the numbers after it, so a GPU MAKO names has a number before anyone lists it.
- What MAKO scales is read from the status file MAKO writes per swapchain, never from its settings:
  with Scaling on MAKO still scales nothing at factor 1 or for a game that asks for the window's full
  size. The file is named by the process and holds no swapchain handle, so the game's own is the one
  whose source or presentation size the swapchain has. MAKO writes no file for a swapchain it does
  nothing on, so no file is a state the card names and never reads as off.
- MAKO's status file says per swapchain whether frame generation is active, and names a reason only
  when it cannot run at all, which MAKO writes for a game drawing to an offscreen X11 window alone. So
  the card calls frame generation paused when the file says it is not active, and gives a cause only
  when MAKO wrote one.
- MAKO's status file has no field for its fall back from Lossless Scaling's model to MAKO FG. MAKO
  changes its model only at the start, so the card reads the fall back from the model the status
  file says is applied against the one MAKO's file named at the start.
- MAKO's power sets are read by MAKO's own rule from `/sys/class/power_supply`, the same check
  comparing it with MAKO's code, and the card names them by what MAKO switches on, plugged in or on
  battery, not docked or handheld.
- The loader keeps a layer's first placement and reads the files of one folder in no set order. So
  the default order layer turns itself off when the other order is asked for.
