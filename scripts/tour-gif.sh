#!/usr/bin/env bash
set -u

[ "$#" -eq 2 ] || { echo "usage: tour-gif <gallery folder> <out.gif>" >&2; exit 2; }
states=(ui-01-effects ui-05-add-effects ui-05b-effects-added fx-06b-pack-annotations fg-04-values gpus-06-timed ui-10-performance ui-09-settings)
frames=()
for state in "${states[@]}"; do
    [ -f "$1/$state.png" ] || { echo "the gallery $1 has no $state.png" >&2; exit 1; }
    frames+=("$1/$state.png")
done
magick -delay 200 -loop 0 "${frames[@]}" -resize 960x -layers Optimize "$2" || { echo "the GIF could not be made" >&2; exit 1; }
echo "wrote $2 from ${#frames[@]} gallery states"
