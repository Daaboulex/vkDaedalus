# Run effects on a second GPU

GPU 1 is always the game's own. Effects can run on the other GPUs of the machine, and the frame always
comes back to GPU 1, so the swapchain, present mode, VRR and vsync stay the game's.

## Move effects to another GPU

1. Open the GPUs card and click Add beside the GPU.
2. Click the effect under its GPU on the GPUs card, or open GPU in its row menu, and pick the GPU. The
   effect and the rows after it run there, until a row picks another GPU.

The card lists under each GPU what runs on it, with frame generation and MAKO's scaling locked to the
game's GPU, and shows every GPU's usage and memory, the trip to the other GPUs and back, and each GPU's
copies in and out while Per-effect GPU timing is on. The game's device gets the sharing extensions only
when its profile lists another GPU at launch; after that, a row moved to that GPU runs there at once.

## In the profile

```ini
gpus = 10de:21c4,1002:13c0
gpus = 1002:7550@0000:73:00.0
invert_input.gpu = 2
```

`gpus` lists GPU 2 and on by vendor and device; the form with `@` names a card by its PCI address, for
two of the same model. A row's `.gpu` applies to it and the rows after it.

## Limits

- The frame crosses through system memory, which costs latency on every frame.
- A row that reads depth, a texture shared across GPUs or a format the other GPU cannot use keeps the
  chain on GPU 1, and the card says why.
- A GPU that fails a hand-off, or takes more than a second for a frame, sends its rows back to GPU 1
  until the next reload.
- NVIDIA needs `nvidia_drm modeset=1`.
