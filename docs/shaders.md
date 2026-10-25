# ReShade shaders

## Add a shader pack

1. Download a pack, for example [reshade-shaders](https://github.com/crosire/reshade-shaders) or
   [CRT-Guest-ReShade](https://github.com/HelelSingh/CRT-Guest-ReShade).
2. On the Shaders card, add the pack's folder. vkDaedalus finds its `Shaders/` and `Textures/` folders.
3. Click Test all shaders to compile every shader once, off the game's frame.
4. On the Effects card, add effects from the pack.

Or place the files in `~/.config/vkdaedalus/reshade/Shaders/` and `Textures/`.

## What runs

When the corpus was last recorded, 512 of the 516 shaders in ReShade's package index compiled to
something vkDaedalus runs. [SHADER-COMPATIBILITY.md](../SHADER-COMPATIBILITY.md) lists them all, why the
other four do not, and which shaders have been run, since compiling is not rendering.

Only a shader's first technique runs. Effects compute at full precision even where a shader asks for
`half`; an effect that declares reduced precision gets a Half precision switch under This effect when
its row is open.

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

## Compiled effects

Compiled effects are kept in `~/.cache/vkdaedalus/` between runs. Clear compiled effects on the Shaders
card empties it and compiles the chain again; the GPU driver's own shader cache is not touched.
