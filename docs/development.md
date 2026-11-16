# Development

With Nix:

```bash
nix develop                  # dev shell with the pre-commit hooks
nix flake check --no-eval-cache
nix build                    # the package, its meson tests included
nix run .#live               # every lane on every GPU of this machine
```

`nix develop` holds only the commit hooks. To build by hand with Nix, enter the package's own build
environment, which has meson and ninja:

```bash
nix develop .#vkdaedalus --command bash -c 'meson setup build; ninja -C build'
```

On any distribution:

```bash
meson setup build
ninja -C build
meson test -C build --print-errorlogs
```

`meson setup` fetches the third-party sources at the commits `flake.lock` pins and checks each against
its hash. That step needs the network; the tests do not.

`scripts/live-host.sh <meson build folder>` runs the same live test on another distribution's own
tools. [TESTING.md](../TESTING.md) lists what each check proves.

[Design](design.md) holds the decisions behind the code.

## Writing a live case

- Keys and clicks go over XTEST. Only the run's own Xwayland takes XTEST; KWin routes it through the
  portal.
- A key held from launch never arrives: Xwayland drops it when the window takes focus. Press it once
  the window has focus.
- The item map lists only items that ImGui both labels and places. A clipped item has no entry, so
  type into a list's search to bring an item into view before clicking it.
- ImGui's `BeginCombo` never labels its item, and the parts of a vector slider carry empty labels.
  The kit names both with `UiMap::nameNextItem`; a new control without a label of its own needs the
  same.

## Reading the layer order

- Read the order of the layers from the loader's "vkCreateInstance layer callstack setup to" block,
  never from its Insert lines. The Insert lines can show an order the instance does not have.
