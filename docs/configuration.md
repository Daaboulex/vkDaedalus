# Configuration

vkDaedalus keeps its files in `~/.config/vkdaedalus/`, made at the first start. Each file has one
writer:

| File | Written by | Holds |
|---|---|---|
| `vkdaedalus.conf` | you | effects and their settings for every program; until you write one, the installed default `share/vkdaedalus/vkdaedalus.conf` is read; the overlay never rewrites it |
| `settings.conf` | the overlay | the keys, the effect limit, auto-apply and the other options of the Settings card |
| `profiles/<name>.conf` | the overlay | a profile any game can run; `default` is the one a game runs until it picks another |
| `configs/<program>.conf` | the overlay | a program's own settings: Safe Anti-Cheat |
| `configs/.active_profiles` | the overlay | which profile each program runs, one `program=profile` per line |
| `shader_manager.conf` | the overlay | the shader and texture folders |

## settings.conf

```ini
maxEffects = 10
toggleKey = Home
frameGenerationKey = Insert
reloadKey = F10
overlayKey = End
enableOnLaunch = true
depthCapture = false
launchNotice = every
autoApply = true
autoApplyDelay = 200
effectGpuTiming = false
liveReshadeUniforms = true
```

Each row of the Settings card ends in a mark: a ring for the default, a filled dot for a value you
changed. The dot's tooltip names the default, and a click on it sets the default back.

## Profiles

A profile holds effects with their values, and these lines:

```ini
gpus = 10de:21c4,1002:13c0
effectsAfterFrameGeneration = false
```

A program's own file holds what belongs to the game, whichever profile it runs:

```ini
safeAntiCheat = true
```

A program is named after its executable. Under Wine and Proton it is named after the `.exe` it runs,
such as `witcher3.exe`. A new Wine game runs the profile that the shared Wine name runs, with Safe
Anti-Cheat on.

In the overlay's profile menu:

- Renaming a profile takes every game that runs it along.
- Deleting a profile sends its games back to `default`.
- Picking `none` asks first, then stops the effects at once and hands the game its own swapchain when
  the game rebuilds it. Until the game closes, the overlay key brings vkDaedalus back. At the next
  start nothing of vkDaedalus loads in that game.

A named profile whose file is missing runs `default`, and the log says so. An effect whose saved file is
gone runs from a file of the same name in `reshadeIncludePath` or a shader folder, and the log names
both.

## Moved from older versions

- A folder from before the rename, `~/.config/vkBasalt-overlay`, moves over at the first start, its
  `vkBasalt.conf` as `vkdaedalus.conf`. Links, such as ones a home-manager module placed, and files the
  new folder already has stay where they are. The old shader cache, `~/.cache/vkBasalt-overlay`, is no
  longer read.
- Profiles from before they were shared, `configs/<program>.conf` and `configs/<program>@<name>.conf`,
  move to `profiles/` at the first start, after `configs/` is copied to `configs.before-shared-profiles`.
  A program's own file becomes the profile named after the program. A named one keeps its name, unless
  two programs had one by that name; then the program's name goes in front. Each program keeps running
  what it ran, with the Safe Anti-Cheat that profile had.
