# AGENTS.md

Instructions for AI assistants. CONTRIBUTING.md applies to you as well; read it first.

@CONTRIBUTING.md

- Working for someone, show a change before making it. "Show me first" means show it and stop.
- Say a change's effect as what someone will see break, and whether git, access or data is touched.
- When a decision is someone else's, give options and name the one you recommend. Act only on the
  options chosen; a remark is a condition, and an unclear one is asked, never guessed.
- Before fixing a failure, say what kind it is: your own mistake, the environment, an upstream
  break, or a real defect. Then fix what let it through as well.
- When you notice a gap, such as something someone would get wrong, a missing check or a doc that no
  longer matches the code, say so and propose the fix: in this project's part below, or as an issue
  in `Daaboulex/nix-packaging-standard` when it concerns every repo.
- Keep this file short and written by people. Add a rule only when someone got something wrong that
  no check catches.

## This project

Decisions and the facts behind them are in [docs/design.md](docs/design.md). Building and testing are
in [docs/development.md](docs/development.md) and [TESTING.md](TESTING.md).

### What the layer never does

- It never crashes or slows a process that is not the game.
- Every claim it makes, in the overlay, the log or a doc, is true.
- No stopgap fix: remove the cause.
- Measure before optimising. No optimisation lands without a before and an after figure from
  `nix run .#live`.

### Commits and pushes

- Do not push until the whole piece of work is done and fully tested.
- Every commit of a stack passes `nix flake check --no-eval-cache` on its own, and every fix has a
  check or test that failed before it.
- The full `nix run .#live` suite runs once, on the last commit of a stack, and every `Test:` line of
  the stack names it.
- A `Test:` line names the check that ran and the build systems it ran for (`aarch64-linux`,
  `x86_64-linux`).

### Checks and tests

- A new invariant gets a check in `flake.nix`. The check counts only once it failed on a planted
  break of the invariant.
- Undo a planted break from a file copy, never with `git checkout`: that discards the uncommitted
  work beside it.
- A planted break never sends work the Vulkan specification forbids to a real GPU.
- A planted break for a live lane never breaks a unit-tested pure function: the package build runs
  `meson test` first and stops there.
- Run `nix run .#live` and `scripts/live-host.sh` with `--parallel`. Planted breaks that trip
  different cases share one run.
- A check that passed on the identical tree is not run again.
- Run a `test/multi-gpu-probe` pair by hand one pair at a time, and read the kernel log after each.
- Scratch work goes into one folder per job under `.cache/`, removed when the work lands.

### Code

- No comments. The exceptions: one terse, timeless line for a fact nothing can check, and a marker a
  check requires. Vendored files keep their own style.
- Every overlay control draws through the kit in `src/overlay/widgets.{hpp,cpp}`. A view calls no
  ImGui widget the kit covers.
- A new module field the renderer reads goes into `putModule`, `getModule` and
  `cacheRoundTripMismatch` in the same change. Without it a warm launch drops the field.
