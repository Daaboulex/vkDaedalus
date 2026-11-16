# Frame generation

Frame generation comes from [lsfg-vk](https://lsfg-vk.dev) or [MAKO](https://github.com/eugeniosegala/MAKO). Each
stays its own layer and is not changed. The Frame generation card edits the layer's own file, the one the layer and
its own settings programs read and write (`lsfg-vk-ui` for lsfg-vk; MAKO's Qt app, `mako-cli` and its Decky plugin for
MAKO), found by the layer's own rules.

## Pick the layer

The card's Layer choice has three entries, and each vkDaedalus profile runs one of them:

- lsfg-vk, which needs `lsfg-vk.dll`.
- MAKO LSFG, which is MAKO running Lossless Scaling's model and needs `Lossless.dll`.
- MAKO FG, which is MAKO running its own model and needs no DLL.

An entry whose layer, DLL or model is missing is greyed with that reason. The layer is kept in the vkDaedalus profile
and MAKO's model in MAKO's profile. `vkdaedalus-run` loads the profile's layer alone and keeps the other out of the
game, and MAKO picks its model when the game starts, so every change of the choice applies from the next launch. When
MAKO cannot start Lossless Scaling's model it runs MAKO FG in its place for that run, which the card marks on MAKO LSFG.

A profile that names no layer runs lsfg-vk when both are installed and the one installed when only one is, so nothing
changes for anyone until they pick MAKO: launch options that set `ENABLE_MAKO=1` without `DISABLE_MAKO` keep getting
MAKO. A layer the profile names always wins.

## Turn it on

1. Install the layer: lsfg-vk 2 ([lsfg-vk.dev](https://lsfg-vk.dev)), with Lossless Scaling in Steam on its lsfg-vk
   branch (Properties, Betas), which ships `lsfg-vk.dll`; or MAKO from its releases.
2. Start the game with `vkdaedalus-run %command%`.
3. Open the Frame generation card, pick the layer and switch it on.
4. Set the multiplier, or for MAKO Adaptive and its target.

The card says first whether the layer is installed where the game's Vulkan loader looks, whether the DLL it needs was
found and whether the layer runs in this game. It also shows this run's order: the game, the effects with the GPUs
they run on, frame generation, MAKO's scaling and the display.

`vkdaedalus-run` finds `lsfg-vk.dll` and `Lossless.dll` in every Steam library that `libraryfolders.vdf` lists and
writes the one it finds into the layer's file. No game gets lsfg-vk switched on while `lsfg-vk.dll` is missing, since
lsfg-vk would then fail the game's swapchain. MAKO needs `Lossless.dll` only for MAKO LSFG and the LS1 scaling methods.

The card's Live, Output and Next launch groups belong to the vkDaedalus profile the game runs; its lsfg-vk or MAKO
group is the layer's own and the same for every game.

## What changes when

| Layer | Changes live | Changes when the game rebuilds its swapchain | Changes at the next launch |
|---|---|---|---|
| lsfg-vk | multiplier, flow scale, performance mode, colour space, transfer function | | vsync, the swapchain image count, half precision, the DLL path, a profile switch |
| MAKO | On, the multiplier, Adaptive and its target, flow scale, MAKO FG mode, performance mode, the pacing group, HDR quality, gamescope VRR, the scaling method and sharpness, a profile switch | the scaling factor, supersampling | Ready at start, the choice between MAKO LSFG and MAKO FG, Ultra performance, Scaling on, Keep swapchain image count, Mailbox compatibility, half precision, `Lossless.dll` |

lsfg-vk reads a live change and has the game rebuild its swapchain. Off sets the multiplier to 1, which passes frames
through, so switching on again needs no new launch. MAKO looks at its file four times a second, applies a live
change in place and picks the game's profile again, so a profile switch or rename takes the new profile's live settings at
once and keeps the next-launch ones it started with. Off sets its frame generation off; On works again in the same run only when Ready at start was on
when the game started, since MAKO sets up frame generation only then. The `Insert` key does the same as the switch.

lsfg-vk joins a game, and MAKO sets up frame generation in it, only if the layer's file holds a profile for the game
when the game starts. `vkdaedalus-run` lists the game in its profile's lsfg-vk profile at multiplier 1, or in its MAKO
profile with frame generation off, before the start, so the first switch needs no relaunch. A game started without it gets lsfg-vk from the launch after the one where it was
first switched on, and never MAKO: MAKO loads only with `ENABLE_MAKO=1`, which `vkdaedalus-run` sets, and the card says so.

## Per profile

Frame generation belongs to a vkDaedalus profile. Its settings sit in the layer's profile `vkdaedalus <profile>`, whose
list names every game that runs the vkDaedalus profile. The list follows when a game picks, renames or loses a profile.
A game that one of your own lsfg-vk or MAKO profiles matches keeps your profile. The card changes only the values that
change and keeps every other byte of the file, comments and other profiles included; MAKO's Qt app and Decky plugin
rewrite the whole file and drop comments and keys they do not know, so either can follow the other. A file or a value the layer
would refuse is left alone, with the layer's reason on the card, and an edit made in the layer's own programs shows on
the card within half a second.

A new lsfg-vk profile takes the colour space of the swapchain of the game that switches it on, which games sharing it
then share.

## Plugged in and on battery

MAKO can keep two sets of a profile's settings and runs the one for the power source it reads: plugged in when any
mains, USB or wireless supply is online, on battery when a battery is there and nothing charges, and the profile's own
values until it can tell. A dock alone changes nothing, and a laptop is on battery when unplugged. The card names
the sets by that, edits the set MAKO uses now unless another tab is picked, and shows the tabs on a machine with a
battery or for a profile that already holds both sets. "Separate settings on battery" makes both sets from the
profile's values, since MAKO needs both, and removing it folds them back. The On switches, the `Insert` key, the tiles
and the notices always act on and show the set MAKO uses now; a tab picks only what the card's settings edit.

## Scaling

MAKO also scales: the game draws at its window's size divided by up to the factor, and MAKO scales each frame up to the
window. The Scaling card appears for a profile that runs MAKO, since scaling runs inside MAKO's own layer.

Scaling switched on is not always scaling: MAKO scales nothing at factor 1, or when the game asks for the window's full
size. MAKO writes what it does for each swapchain into a status file in `runtime-state` beside its own file, and the
card, its tile and the run's order read the game's own one. They call scaling on only when MAKO reports it active, with
the size the game draws at and the size MAKO presents. When MAKO reports it inactive they give MAKO's reason on the On
switch, and while MAKO has written no such file they say that there is no status file. MAKO writes one only for a
swapchain it works on: where it neither scales nor can generate frames it shows the game's own frames and writes
none.

## Effects before or after frame generation

The order of the layers is fixed when a game starts, so you choose it per profile:

- Effects before frame generation, the default: effects run once per real frame, which costs least, and the layer
  builds the frames between from them.
- Effects after frame generation: effects run on every frame shown, so an effect that the layer's motion estimation
  would warp, like a CRT filter, looks right, at the multiplier times the cost.

Tick "Effects after frame generation" on the card. A game started through `vkdaedalus-run` picks it up at its next
launch. [How it works](how-it-works.md#the-order-around-frame-generation) explains the order layers behind it.

## Which GPU

Frame generation runs on the GPU the game renders on. The card's GPU row names that GPU and offers no other. lsfg-vk 2
has no GPU setting: it hands each frame to its own device as memory only the same GPU can import. MAKO supports no frame
generation on another GPU than the game's; its file can name a GPU, which the card never writes, and when it names
another than the game's the GPU row says so and offers to clear it. Effects can still run on another GPU, as
[a second GPU](second-gpu.md) describes.
