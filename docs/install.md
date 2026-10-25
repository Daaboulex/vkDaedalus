# Install

## NixOS

1. Add vkDaedalus as a flake input:

   ```nix
   inputs.vkdaedalus = {
     url = "github:Daaboulex/vkDaedalus";
     inputs.nixpkgs.follows = "nixpkgs";
   };
   ```

2. Add its overlay:

   ```nix
   nixpkgs.overlays = [ inputs.vkdaedalus.overlays.default ];
   ```

3. Install the layer, and the 32-bit build for older Wine games that run without WoW64. Each manifest
   names its architecture, so the Vulkan loader picks the right one:

   ```nix
   hardware.graphics = {
     enable32Bit = true;
     extraPackages = [ pkgs.vkdaedalus ];
     extraPackages32 = [ inputs.vkdaedalus.packages.x86_64-linux.vkdaedalus-i686 ];
   };
   ```

## Arch and CachyOS

```bash
cd packaging/arch
makepkg -si
```

The package replaces the earlier `vkbasalt-overlay-wayland-git` and installs beside upstream `vkbasalt`,
which loads only when `ENABLE_VKBASALT` is set. If both run in one game, effects apply twice and
vkDaedalus says so in its log.

## Other distributions

Build with meson and install the result:

```bash
meson setup build
ninja -C build
meson install -C build
```

`meson setup` fetches the third-party sources at the commits `flake.lock` pins and checks each against
its hash, so that step needs a network connection.

Next: [use vkDaedalus in a game](use.md).
