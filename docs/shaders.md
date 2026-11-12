# ReShade shaders

## Add a shader pack

1. Download a pack, for example [reshade-shaders](https://github.com/crosire/reshade-shaders) or
   [CRT-Guest-ReShade](https://github.com/HelelSingh/CRT-Guest-ReShade).
2. On the Shaders card, add the pack's folder. vkDaedalus finds its `Shaders/` and `Textures/` folders.
3. Click Test all shaders to compile every shader once, off the game's frame.
4. On the Effects card, add effects from the pack.

Or place the files in `~/.config/vkdaedalus/reshade/Shaders/` and `Textures/`.

## What runs

When the corpus was last recorded, 513 of the 517 shaders in ReShade's package index compiled to
something vkDaedalus runs. [SHADER-COMPATIBILITY.md](../SHADER-COMPATIBILITY.md) lists them all, why the
other four do not, and which shaders have been run, since compiling is not rendering.

Effects compute at full precision even where a shader asks for `half`; an effect that declares reduced
precision gets a Half precision switch under This effect when its row is open.

## A file's techniques

Every technique of a file in the profile has a row, as in ReShade. The file's own row runs its first
technique. Each other technique joins after it, named `<effect>/<technique>` in the profile, off unless
the profile lists it. A file's rows share its parameters, textures and uniform values, and each runs
where it sits in the chain. The overlay reads the annotations ReShade reads on a technique:

| Annotation | What the row does |
|---|---|
| `ui_label`, `ui_tooltip` | the row reads `<effect>: <label>` and shows the tooltip |
| `enabled` | on at every start, whatever the profile says; switched off, it stays off for the run |
| `timeout` | switched off that many milliseconds after it is turned on |
| `hidden` | no row; it still runs while the profile lists it on |

A row's menu sets a key that turns the row on and off while the game runs, saved as `<row>.key`.
The key works while the effects are on and nothing in the overlay is being typed into, as in ReShade.
Remove takes the file out with every technique it has. With Safe Anti-Cheat on, only a technique that
reads the depth buffer is left out; the file's other techniques run.

## A pack's parameters

An open effect lists its parameters as rows: the name, the control, the value and, at the end, the
default mark. A filled mark is a value changed from the default, which its tooltip names, and a click
sets it back. The overlay reads the annotations ReShade's own interface reads:

| Annotation | What the row does |
|---|---|
| `ui_label`, `ui_tooltip` | the row's name, cut with an ellipsis when long, and its tooltip with the full name |
| `ui_type`, `ui_min`, `ui_max`, `ui_step` | a slider, a switch for a `bool`, a colour swatch that opens a picker for `"color"` |
| `ui_items` | a menu, or with `ui_type = "radio"` a row of up to four choices that fit |
| `ui_units` | follows the value, as `2.50 m` |
| `ui_category`, `ui_category_closed` | a caption that opens and closes, closed at first when the author asks |
| `ui_text` | the author's text, behind the i on the effect's row; a `"radio"` with no items carries text only and takes no row |
| `hidden` | no row; a saved value still reaches the uniform |

## Definitions and Apply

A parameter's change runs at once. A preprocessor definition, an effect's own or one of the definitions
for every effect, compiles into the effect, so it applies when the chain is rebuilt: by Apply, the Apply
key or a change to the profile's file. Until then the Effects card's footer says Waits for Apply. With
Apply automatically off on the Settings card, a change to the chain waits the same way.

## The headers packs expect

Most packs include `ReShade.fxh` and `ReShadeUI.fxh`, which ReShade itself installs beside them.
vkDaedalus ships both, searched after every shader folder, so a pack copied without them still compiles.
`ReShade.fxh` is crosire's (CC0-1.0, from reshade-shaders' slim branch), and `ReShadeUI.fxh` is
vkDaedalus' own, with the same widget macros for the ReShade version it reports. A pack's own copy, in its
folder or a shader folder, wins over the shipped one.

## What a shader is told

Every shader gets the macros ReShade defines, with the values ReShade would give in the same place:

| Macro | Value |
|---|---|
| `__RESHADE__` | The ReShade version whose runtime this build keeps |
| `__RESHADE_PERMUTATION__` | 0, the back buffer's own |
| `__RESHADE_PERFORMANCE_MODE__` | 0 with Instant ReShade sliders on, else 1 |
| `__VENDOR__`, `__DEVICE__` | The PCI ids of the GPU the effect runs on |
| `__RENDERER__` | `0x20000` with the Vulkan version the game runs at, `0x21300` for 1.3 |
| `__APPLICATION__` | The program's name hashed the way ReShade hashes it on Windows |
| `BUFFER_WIDTH`, `BUFFER_HEIGHT`, `BUFFER_RCP_WIDTH`, `BUFFER_RCP_HEIGHT` | The back buffer's size |
| `BUFFER_COLOR_FORMAT` | The back buffer's format in ReShade's numbering, sRGB counted as its linear twin |
| `BUFFER_COLOR_BIT_DEPTH` | The bits per colour channel of that format |
| `BUFFER_COLOR_SPACE` | ReShade's colour space of the swapchain: 1 sRGB, 2 scRGB, 3 HDR10 PQ, 4 HDR10 HLG |

A texture declared with `Format = BUFFER_COLOR_FORMAT` takes the back buffer's format, a BGRA one
included.

## Values a shader reads

A uniform with a `source` annotation gets its value from vkDaedalus, computed once per frame for each
file and converted to the type the uniform declares, as ReShade does:

| `source` | Value |
|---|---|
| `timer` | milliseconds since the game's first frame in this window, kept through a resize |
| `frametime` | milliseconds between the last two frames |
| `framecount` | the frames before this one; a `bool` is true on every other frame |
| `random` | a number from `min` to `max`, 0 to 32767 by default, new each frame |
| `pingpong` | a value moving between `min` and `max`, 0 and 1 by default, at the speed in `step` |
| `date` | the year, month, day and seconds since midnight |
| `key`, `mousebutton` | whether the key or button named by `keycode` is down, or pressed this frame, or toggled, as the annotations ask |
| `mousepoint`, `mousedelta` | where the pointer is in the frame, and how far it moved since the last frame |
| `mousewheel` | the wheel's position, moved by `step` per notch and kept between `min` and `max` when they differ; the second value is this frame's notches |
| `overlay_open`, `ui_open` | whether the overlay is shown |
| `overlay_active`, `ui_active`, `overlay_hovered`, `ui_hovered` | 1 plus the index of the file's parameter being edited or under the pointer, 0 for none |
| `screenshot` | always false: vkDaedalus takes no screenshots |
| `bufready_depth` | whether a depth buffer reaches the effect |

A source vkDaedalus does not provide is named in the log, and nothing writes that uniform.

## Compiled effects

Compiled effects are kept in `~/.cache/vkdaedalus/` between runs. Clear compiled effects on the Shaders
card empties it and compiles the chain again; the GPU driver's own shader cache is not touched.
