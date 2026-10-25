# Use vkDaedalus in a game

## Steam

1. Set the game's launch options to:

   ```text
   vkdaedalus-run %command%
   ```

2. Start the game and press `End` to open the overlay.

`vkdaedalus-run` loads the layer, preloads the input hook that keeps the game's input while the overlay
is open, and keeps vkDaedalus to the game: the program it starts and the other programs in that game's
folder. Every other process under it loads nothing.

## Pick a profile

Start the game on a profile:

```text
vkdaedalus-run --profile <name> %command%
```

Print or set a game's profile outside a launch:

```bash
vkdaedalus-profile <game> [<profile>]
```

Either one brings vkDaedalus back to a game set to `none`.

## Lutris and other launchers

Set `ENABLE_VKDAEDALUS=1` for the game. In Lutris it goes under System options, Environment variables.
Every Vulkan program the command starts then loads the layer. Without the input hook that
`vkdaedalus-run` preloads, the overlay cannot hold a Wayland game's input, and on X11 it falls back to a
keyboard and pointer grab, which a game holding the pointer refuses.

To try the layer outside a game:

```bash
ENABLE_VKDAEDALUS=1 vkcube
```

## gamescope

gamescope removes `ENABLE_VKDAEDALUS` from the program it starts, so start `vkdaedalus-run` inside it:

```text
gamescope -W 2560 -H 1440 -- vkdaedalus-run %command%
```

Started outside gamescope, the layer applies to gamescope's own output instead.

See [keys](keys.md) for the overlay's keys and [configuration](configuration.md) for the files it keeps.
