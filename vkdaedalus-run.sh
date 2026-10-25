#!/bin/sh

if [ $# -eq 0 ] || [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
  echo "Usage: vkdaedalus-run [--profile <name>] <command...>"
  echo ""
  echo "Launch a game with the vkDaedalus layer enabled."
  echo "Sets ENABLE_VKDAEDALUS=1, preloads the input hook that lets the overlay block the game's Wayland input,"
  echo "and VKDAEDALUS_AFTER_LSFGVK=1 when the game's profile runs its effects after frame generation;"
  echo "its two order layers, read before any other, put vkDaedalus and lsfg-vk in that order."
  echo "With lsfg-vk installed and its DLL found, a game lsfg-vk has no profile for gets one at multiplier 1,"
  echo "so frame generation can be switched on in the game's overlay without a new launch."
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

export ENABLE_VKDAEDALUS=1
export VK_ADD_IMPLICIT_LAYER_PATH="@order_layers@${VK_ADD_IMPLICIT_LAYER_PATH:+:$VK_ADD_IMPLICIT_LAYER_PATH}"
export VKDAEDALUS_RUN=1
unset VKDAEDALUS_AFTER_LSFGVK

frame_order() {
  if [ -n "$profile" ]; then
    LD_PRELOAD='' "@frame_order@" --profile "$profile" "$@"
  else
    LD_PRELOAD='' "@frame_order@" "$@"
  fi
}

if answer=$(frame_order "$@"); then
  { read -r order; read -r VKDAEDALUS_RUN_PROGRAM; read -r VKDAEDALUS_RUN_EXECUTABLE; } <<EOF
$answer
EOF
  export VKDAEDALUS_RUN_PROGRAM VKDAEDALUS_RUN_EXECUTABLE
  if [ "$order" = after ]; then
    export VKDAEDALUS_AFTER_LSFGVK=1
  fi
else
  echo "vkdaedalus-run: the game's frame generation order could not be read, so its effects run before frame generation" >&2
fi

input_hook="@input_hook@"
if [ -f "$input_hook" ]; then
  export LD_PRELOAD="${LD_PRELOAD:+$LD_PRELOAD:}$input_hook"
fi

exec "$@"
