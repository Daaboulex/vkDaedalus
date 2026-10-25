# Development

With Nix:

```bash
nix develop                  # dev shell with the pre-commit hooks
nix flake check --no-eval-cache
nix build                    # the package, its meson tests included
nix run .#live               # every lane on every GPU of this machine
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
