# How it works

## The frame

vkDaedalus is an implicit Vulkan layer: the Vulkan loader puts it between the game and the driver. It
gives the game its own images for the swapchain. At each present it runs the effect chain from those
images into the real ones and draws the overlay on top, then passes the present on. The swapchain, its
present mode, VRR and vsync stay the game's.

Effects you add compile off the game's frame, on a thread of their own. Compiled effects land in a
cache keyed by the running build, so a new build compiles once and never reads another build's entries.

## Input

The overlay reads the mouse and keys through its own X11 connection or its own Wayland event queue.
It reads the mouse only while it is open or a running effect reads the mouse, and takes typed keys
only while it is open. Closed, it draws only the HUD and the launch notice when they are on, and each
card computes its figures only while it is the card shown.
While it is open, the input hook that `vkdaedalus-run` preloads drops the game's keys, buttons and
motion: in libxcb's event reads on X11 and in the game's own Wayland listeners. Releases still reach
the game, so a key held when the overlay opened does not stick. An X11 game started without
`vkdaedalus-run` gets a keyboard and pointer grab instead.

## The order around lsfg-vk

vkDaedalus and lsfg-vk are both implicit layers, and the loader fixes their order when the game
creates its Vulkan instance. So the order is chosen per profile and applies from the next start.

`vkdaedalus-run` puts the folder `share/vkdaedalus/order-layers` first in `VK_ADD_IMPLICIT_LAYER_PATH`,
which the loader reads before every other implicit layer folder. It holds two order layers. Each is a
meta layer that names the two layers in one order, and the loader keeps the first placement it reads:

| Order layer | Nearest the game | Active |
|---|---|---|
| `VK_LAYER_DAABOULEX_vkdaedalus_before_lsfgvk` | vkDaedalus: effects run on the game's own frames, and lsfg-vk builds the frames between from them | by default |
| `VK_LAYER_DAABOULEX_lsfgvk_before_vkdaedalus` | lsfg-vk: effects run on every frame shown | with `VKDAEDALUS_AFTER_LSFGVK=1` |

Before the game starts, `vkdaedalus-run` clears any `VKDAEDALUS_AFTER_LSFGVK` it inherited, reads the
game's profile and sets the variable when the profile asks for effects after frame generation. A game
started with `ENABLE_VKDAEDALUS=1` alone reads no order layer, so the loader places the two layers in
the order it finds their files. The Performance card's lsfg-vk line shows the order this run got, read
from the layers below vkDaedalus.

## A second GPU

GPU 1 is the game's. A profile can list more GPUs, and a row of the chain can run on one of them. The
chain splits into runs of rows on one GPU. At each crossing the frame goes through system memory as a
shared buffer, with a sync file handing it from one GPU to the next, and the frame always ends on
GPU 1. A GPU that fails a hand-off, or takes more than a second for a frame, sends its rows back to
GPU 1 until the next reload.

## When something fails

Every Vulkan call vkDaedalus makes fails closed. An effect that cannot be built is replaced by a copy
that passes the frame through, a swapchain it cannot set up gets the game's own images, and an overlay
that cannot draw switches off and lets the input go. The live test's fault lane fails each of these
calls once, under the Khronos validation layer, and the game must run on.

[TESTING.md](../TESTING.md) describes every check and [LATENCY.md](../LATENCY.md) every timing figure.
