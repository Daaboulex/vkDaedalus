# Run effects on a second GPU

GPU 1 is always the game's own. Effects can run on the other GPUs of the machine, and the frame always
comes back to GPU 1, so the swapchain, present mode, VRR and vsync stay the game's.

## Move effects to another GPU

1. Open the GPUs card and click Add beside the GPU.
2. In an effect's row menu, open GPU and pick it. The effect and the rows after it run there, until a row
   picks another GPU.

The card shows every GPU's usage and memory, the trip to the other GPUs and back, and each GPU's copies
in and out while Per-effect GPU timing is on. The game's device gets the sharing extensions only when its
profile lists another GPU at launch; after that, a row moved to that GPU runs there at once.

## In the profile

```ini
gpus = 10de:21c4,1002:13c0
gpus = 1002:7550@0000:73:00.0
invert_input.gpu = 2
```

`gpus` lists GPU 2 and on by vendor and device; the form with `@` names a card by its PCI address, for
two of the same model. A row's `.gpu` applies to it and the rows after it.

## Limits

- The frame crosses through system memory, which costs latency on every frame. For scale, the copies
  alone took about 3.3 ms there and back for a 1920x1080 frame to a GTX 1660 SUPER in a PCIe 3.0 x8 slot.
- A row that reads depth, a texture shared across GPUs or a format the other GPU cannot use keeps the
  chain on GPU 1, and the card says why.
- A GPU that fails a hand-off, or takes more than a second for a frame, sends its rows back to GPU 1
  until the next reload.
- NVIDIA needs `nvidia_drm modeset=1`.
