# Frame generation

Frame generation comes from [lsfg-vk](https://github.com/PancakeTAS/lsfg-vk), which stays its own layer
and is not changed. The Frame generation card edits lsfg-vk's own file, the one lsfg-vk and its settings
program `lsfg-vk-ui` read and write, found by lsfg-vk's own rules.

## Turn it on

1. Install lsfg-vk 2 ([lsfg-vk.dev](https://lsfg-vk.dev)), and Lossless Scaling in Steam on its lsfg-vk
   branch (Properties, Betas), which ships `lsfg-vk.dll`.
2. Start the game with `vkdaedalus-run %command%`.
3. Open the Frame generation card and switch it on.
4. Set the multiplier.

The card says first whether lsfg-vk is installed where the game's Vulkan loader looks, whether
`lsfg-vk.dll` was found and whether lsfg-vk runs in this game. It also shows this run's order: the game,
the effects with the GPUs they run on, frame generation and the display.

`vkdaedalus-run` finds the DLL in every Steam library that `libraryfolders.vdf` lists and writes the one
it finds into lsfg-vk's file. No game is switched on while the DLL is missing, since lsfg-vk would then
fail the game's swapchain.

The card's Live, Output and Next launch groups belong to the vkDaedalus profile the game runs; its
lsfg-vk group is lsfg-vk's own and the same for every game.

## What changes when

| Changes live | Changes at the next launch |
|---|---|
| multiplier, flow scale, performance mode, colour space, transfer function | vsync, the swapchain image count, half precision, the DLL path, a profile switch |

lsfg-vk reads a live change and has the game rebuild its swapchain. Off sets the multiplier to 1, which
passes frames through, so switching on again needs no new launch. The `Insert` key does the same.

lsfg-vk joins a game only if its file holds a profile for the game when the game starts.
`vkdaedalus-run` lists the game in its profile's lsfg-vk profile before the start, so the first switch
needs no relaunch. A game started without it gets frame generation from the launch after the one where it
was first switched on.

## Per profile

Frame generation belongs to a vkDaedalus profile. Its settings sit in the lsfg-vk profile
`vkdaedalus <profile>`, whose list names every game that runs the vkDaedalus profile. The list follows
when a game picks, renames or loses a profile. A game that one of your own lsfg-vk profiles matches keeps
your profile. The card changes only the values that change and keeps every other byte of the file,
comments and other profiles included. A file or a value lsfg-vk would refuse is left alone, with
lsfg-vk's reason on the card, and an edit made in `lsfg-vk-ui` shows on the card within half a second.

A new lsfg-vk profile takes the colour space of the swapchain of the game that switches it on, which
games sharing it then share.

## Effects before or after frame generation

The order of the two layers is fixed when a game starts, so you choose it per profile:

- Effects before frame generation, the default: effects run once per real frame, which costs least, and
  lsfg-vk builds the frames between from them.
- Effects after frame generation: effects run on every frame shown, so an effect that lsfg-vk's motion
  estimation would warp, like a CRT filter, looks right, at the multiplier times the cost.

Tick "Effects after frame generation" on the card. A game started through `vkdaedalus-run` picks it up
at its next launch. [How it works](how-it-works.md#the-order-around-lsfg-vk) explains the two order
layers behind it.

## Which GPU

lsfg-vk runs on the GPU the game renders on. lsfg-vk 2 has no GPU setting: it hands each frame to its
own device as memory only the same GPU can import. Effects can still run on another GPU, as
[a second GPU](second-gpu.md) describes.
