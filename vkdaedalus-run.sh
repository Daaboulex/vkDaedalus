#!/bin/sh

if [ $# -eq 0 ] || [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
  echo "Usage: vkdaedalus-run [--profile <name>] <command...>"
  echo ""
  echo "Launch a game with the vkDaedalus layer enabled."
  echo "Sets ENABLE_VKDAEDALUS=1 and preloads the input hook that lets the overlay block the game's Wayland input."
  echo "Loads only the frame generation layer the game's profile runs: for MAKO it sets ENABLE_MAKO=1 and DISABLE_LSFGVK=1,"
  echo "for lsfg-vk DISABLE_MAKO=1, and VKDAEDALUS_RUN_FRAME_GENERATION_LAYER names the layer it loads, lsfg-vk or mako."
  echo "VKDAEDALUS_RUN_CALLER_DISABLED names the DISABLE_LSFGVK and DISABLE_MAKO the game was started with, before it sets its own."
  echo "Started by another vkdaedalus-run, it first drops the DISABLE_LSFGVK and DISABLE_MAKO that run set and did not record."
  echo "VKDAEDALUS_AFTER_MAKO=1 or VKDAEDALUS_AFTER_LSFGVK=1 is set when the profile runs its"
  echo "effects after frame generation; its order layers, read before any other, put vkDaedalus and that layer in that order."
  echo "With the profile's layer installed, a game it has no profile for gets one before it starts, so frame generation can"
  echo "be switched on in the game's overlay without a new launch: lsfg-vk's at multiplier 1 once its DLL is found, MAKO's"
  echo "with frame generation off."
  echo "--profile <name> sets the profile the game runs first, which also brings vkDaedalus back to a game set to none."
  exit 0
fi

profile=""
if [ "$1" = "--profile" ]; then
  if [ $# -lt 3 ]; then
    echo "vkdaedalus-run: --profile needs a profile name and a command" >&2
    exit 2
  fi
  profile=$2
  shift 2
fi

if [ -n "${VKDAEDALUS_RUN+set}" ] && [ -n "${VKDAEDALUS_RUN_CALLER_DISABLED+set}" ]; then
  for outer in DISABLE_LSFGVK DISABLE_MAKO; do
    case " $VKDAEDALUS_RUN_CALLER_DISABLED " in
      *" $outer "*) ;;
      *) unset "$outer" ;;
    esac
  done
fi

export ENABLE_VKDAEDALUS=1
export VK_ADD_IMPLICIT_LAYER_PATH="@order_layers@${VK_ADD_IMPLICIT_LAYER_PATH:+:$VK_ADD_IMPLICIT_LAYER_PATH}"
export VKDAEDALUS_RUN=1
caller_disabled=""
if [ -n "${DISABLE_LSFGVK+set}" ]; then
  caller_disabled="DISABLE_LSFGVK"
fi
if [ -n "${DISABLE_MAKO+set}" ]; then
  caller_disabled="${caller_disabled:+$caller_disabled }DISABLE_MAKO"
fi
export VKDAEDALUS_RUN_CALLER_DISABLED="$caller_disabled"
unset VKDAEDALUS_AFTER_LSFGVK VKDAEDALUS_AFTER_MAKO VKDAEDALUS_RUN_FRAME_GENERATION_LAYER

frame_order() {
  if [ -n "$profile" ]; then
    LD_PRELOAD='' "@frame_order@" --profile "$profile" "$@"
  else
    LD_PRELOAD='' "@frame_order@" "$@"
  fi
}

layer=lsfg-vk
order=before
if answer=$(frame_order "$@"); then
  { read -r order; read -r VKDAEDALUS_RUN_PROGRAM; read -r VKDAEDALUS_RUN_EXECUTABLE; read -r layer; } <<EOF
$answer
EOF
  export VKDAEDALUS_RUN_PROGRAM VKDAEDALUS_RUN_EXECUTABLE
else
  echo "vkdaedalus-run: the game's profile could not be read, so MAKO is kept out and its effects run before frame generation" >&2
fi

export VKDAEDALUS_RUN_FRAME_GENERATION_LAYER="$layer"
if [ "$layer" = mako ]; then
  export ENABLE_MAKO=1 DISABLE_LSFGVK=1
  if [ "$order" = after ]; then
    export VKDAEDALUS_AFTER_MAKO=1
  fi
else
  unset ENABLE_MAKO
  export DISABLE_MAKO=1
  if [ "$order" = after ]; then
    export VKDAEDALUS_AFTER_LSFGVK=1
  fi
fi

input_hook="@input_hook@"
if [ -f "$input_hook" ]; then
  export LD_PRELOAD="${LD_PRELOAD:+$LD_PRELOAD:}$input_hook"
fi

exec "$@"
