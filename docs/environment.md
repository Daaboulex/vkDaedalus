# Environment variables

| Variable | Read by | What it does |
|---|---|---|
| `ENABLE_VKDAEDALUS=1` | Vulkan loader | Loads the layer into the program; `vkdaedalus-run` sets it. Under gamescope the layer runs in the game and stays out of gamescope's own process, unless `configs/.active_profiles` names a profile for gamescope, so effects apply once. |
| `ENABLE_VKBASALT=1` | Vulkan loader | Upstream vkBasalt's. This layer does not read it; when upstream vkBasalt runs beside it, the layer warns, as effects then apply twice. |
| `DISABLE_VKDAEDALUS=1` | Vulkan loader | Keeps the layer out, even when enabled. |
| `VKDAEDALUS_CONFIG_FILE=<path>` | layer | Runs this config file instead of the program's profile; Safe Anti-Cheat is then read from it. |
| `VKDAEDALUS_LOG_LEVEL=<level>` | layer | `trace`, `debug`, `info`, `warn`, `error` or `none`, for this run, over the Log level chosen on the Debug card. |
| `VKDAEDALUS_LAUNCH_NOTICE=<when>` | overlay | `every`, `first` or `never`, for this run, over the Launch notice chosen in Settings: the card that says vkDaedalus loaded for 5 seconds after a game starts (8 when an effect could not run). |
| `VKDAEDALUS_LOG_FILE=<path>` | layer | Appends the log to this file instead of stderr; without it the log goes to stderr and to `$XDG_STATE_HOME/vkdaedalus/vkdaedalus.log`. |
| `VKDAEDALUS_AFTER_LSFGVK=1` | Vulkan loader, through the order layers | Runs the layer's effects after lsfg-vk's frame generation for this run instead of before. `vkdaedalus-run` sets it when the game's profile asks for it. |
| `VKDAEDALUS_RUN=1`, `VKDAEDALUS_RUN_PROGRAM=<name>`, `VKDAEDALUS_RUN_EXECUTABLE=<path>` | layer, overlay | Set by `vkdaedalus-run`: the layer keeps to that program and the other programs of its game folder, so the helpers a game starts (Wine's explorer.exe, Proton's scripts) load nothing; the Frame generation page uses them to say whether the next launch picks up a changed order. |
| `LD_PRELOAD` | dynamic linker | `vkdaedalus-run` adds `libvkdaedalus-input-hook.so`, which wraps the game's Wayland pointer and keyboard listeners and libxcb's event reads from the game's start, Wine's included, so the overlay can keep input from the game while it is open. |
| `LSFGVK_CONFIG=<path>` | lsfg-vk, overlay, `vkdaedalus-run` | Names lsfg-vk's file instead of `~/.config/lsfg-vk/conf.toml` (or `$XDG_CONFIG_HOME/lsfg-vk/conf.toml`). The overlay and `vkdaedalus-run` find the file by lsfg-vk's own rules, this one first. |
| `LSFGVK_FLATPAK` | lsfg-vk, overlay | Set inside a Flatpak: lsfg-vk then keeps its file at `~/.config/lsfg-vk/conf.toml` whatever `$XDG_CONFIG_HOME` says, and the overlay looks there too. |
| `LSFGVK_PROFILE=<name>`, `SteamAppId` | lsfg-vk, overlay | lsfg-vk picks its profile by this name, else by the game's executable, its `.exe`, its process name and last its Steam app id; the overlay finds the profile in use the same way. |
| `LSFGVK_ENV` | lsfg-vk, overlay | lsfg-vk takes its settings from its `LSFGVK_` variables instead of its file; the Frame generation page then says so and changes nothing. |
| `DISABLE_LSFGVK=1` | lsfg-vk | Keeps lsfg-vk out of the program. |
| `LSFGVK_DLL_PATH=<path>` | lsfg-vk, overlay | Where `lsfg-vk.dll` is, over the `dll` in lsfg-vk's file. Without either, lsfg-vk looks only in the Steam folders under the home; the overlay and `vkdaedalus-run` also look in every Steam library `libraryfolders.vdf` lists and write the one found into lsfg-vk's file. No game is switched on while the DLL is found nowhere, since lsfg-vk would then fail the game's swapchain. |
| `DISABLE_VKDAEDALUS_LSFGVK_ORDER=1` | Vulkan loader | Turns off the after-frame-generation order layer. |
| `VKDAEDALUS_UI_MAP=<path>` | layer | For tests: writes the overlay's items and their places to this file after every frame. Unset, nothing runs. |
| `XDG_CONFIG_HOME`, `XDG_CACHE_HOME`, `XDG_DATA_HOME`, `HOME` | layer | Where the config (`~/.config/vkdaedalus`), the shader cache (`~/.cache/vkdaedalus`) and an older config location are. |
| `WINEPREFIX` | layer | Finds a Wine game's folder for the shipped anti-cheat check. |
| `DISPLAY`, `WAYLAND_DISPLAY` | layer | Chooses X11 or Wayland input. |
