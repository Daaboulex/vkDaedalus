#!/usr/bin/env bash
set -euo pipefail

if [ $# -ne 3 ]; then
  echo "usage: check.sh <vkdaedalus-run> <order layer folder> <work folder>" >&2
  exit 2
fi
run=$1
orders=$2
work=$3
failures=0
cases=0
ours=VK_LAYER_VKDAEDALUS_post_processing
lsfg=VK_LAYER_LSFGVK_frame_generation
mako=VK_LAYER_MAKO_render
shell=$(command -v sh)
printer=$(command -v env)

fail() {
  echo "$1"
  failures=$((failures + 1))
}

manifest() {
  local folder=$1 name=$2 enable=$3 disable=$4
  mkdir -p "$folder"
  printf '{"file_format_version": "1.2.0", "layer": {"name": "%s", "type": "GLOBAL", "library_path": "%s", "api_version": "1.3.0", "implementation_version": "1", "description": "a layer the loader lists and never loads", %s"disable_environment": {"%s": "1"}}}\n' \
    "$name" "lib$name-stand-in.so" "$enable" "$disable" >"$folder/$name.json"
}

manifest "$work/layers/lsfg-vk" "$lsfg" "" DISABLE_LSFGVK
manifest "$work/layers/mako" "$mako" '"enable_environment": {"ENABLE_MAKO": "1"}, ' DISABLE_MAKO
manifest "$work/layers/both" "$lsfg" "" DISABLE_LSFGVK
manifest "$work/layers/both" "$mako" '"enable_environment": {"ENABLE_MAKO": "1"}, ' DISABLE_MAKO
mkdir -p "$work/layers/none"

order_layers_on() {
  local environment=$1
  for file in "$orders"/*.json; do
    jq -r --rawfile environment "$environment" '
      ($environment | split("\n") | map(select(contains("="))) | map(split("=") | {(.[0]): (.[1:] | join("="))}) | add // {}) as $set
      | .layer
      | select(((.enable_environment // {}) | to_entries | all(. as $e | $set[$e.key] == $e.value))
          and (((.disable_environment // {}) | to_entries | any(. as $e | $set | has($e.key))) | not))
      | .component_layers | join(" ")' "$file"
  done
}

value_of() {
  sed -n "s/^$1=//p" "$2"
}

expect_run() {
  local label=$1 key=$2 layers=$3 want_layer=$4 want_order=$5
  shift 5
  cases=$((cases + 1))
  local home="$work/case-$cases"
  mkdir -p "$home/.config/vkdaedalus/profiles"
  printf '%s' "$key" >"$home/.config/vkdaedalus/profiles/default.conf"
  env -i HOME="$home" XDG_CONFIG_HOME="$home/.config" XDG_DATA_HOME="$home/.local/share" VK_ADD_IMPLICIT_LAYER_PATH="$work/layers/$layers" "$@" \
    "$run" "$shell" -c "$printer" Game.exe >"$home/environment" 2>"$home/errors" || {
    fail "$label: vkdaedalus-run exited with $?: $(cat "$home/errors")"
    return
  }
  local on want
  on=$(order_layers_on "$home/environment")
  if [ "$want_layer" = mako ]; then
    want="$mako $ours"
    if [ "$want_order" = before ]; then want="$ours $mako"; fi
    [ "$(value_of ENABLE_MAKO "$home/environment")" = 1 ] || fail "$label: ENABLE_MAKO is not 1, so MAKO stays out of a game whose profile runs it"
    [ "$(value_of DISABLE_LSFGVK "$home/environment")" = 1 ] || fail "$label: DISABLE_LSFGVK is not 1, so lsfg-vk joins a game whose profile runs MAKO"
    [ -z "$(value_of VKDAEDALUS_AFTER_LSFGVK "$home/environment")" ] || fail "$label: VKDAEDALUS_AFTER_LSFGVK is set for a profile running MAKO"
  else
    want="$lsfg $ours"
    if [ "$want_order" = before ]; then want="$ours $lsfg"; fi
    [ "$(value_of DISABLE_MAKO "$home/environment")" = 1 ] || fail "$label: DISABLE_MAKO is not 1, so MAKO joins a game whose profile runs lsfg-vk"
    [ -z "$(value_of ENABLE_MAKO "$home/environment")" ] || fail "$label: ENABLE_MAKO is still set for a profile running lsfg-vk"
    [ -z "$(value_of VKDAEDALUS_AFTER_MAKO "$home/environment")" ] || fail "$label: VKDAEDALUS_AFTER_MAKO is set for a profile running lsfg-vk"
  fi
  [ "$on" = "$want" ] || fail "$label: the order layers on are '${on//$'\n'/ | }', want exactly '$want'"
}

expect_run "MAKO, effects before" $'frameGenerationLayer = mako\n' none mako before
expect_run "MAKO, effects after" $'frameGenerationLayer = mako\neffectsAfterFrameGeneration = true\n' none mako after
expect_run "lsfg-vk, effects before" $'frameGenerationLayer = lsfg-vk\n' none lsfg-vk before
expect_run "lsfg-vk, effects after" $'frameGenerationLayer = lsfg-vk\neffectsAfterFrameGeneration = true\n' none lsfg-vk after
expect_run "lsfg-vk, started with MAKO's variables and both orders set" $'frameGenerationLayer = lsfg-vk\n' none lsfg-vk before \
  ENABLE_MAKO=1 VKDAEDALUS_AFTER_MAKO=1 VKDAEDALUS_AFTER_LSFGVK=1
expect_run "MAKO, started with lsfg-vk's after order set" $'frameGenerationLayer = mako\n' none mako before VKDAEDALUS_AFTER_LSFGVK=1
expect_run "no choice, nothing installed" '' none lsfg-vk before
expect_run "no choice, lsfg-vk installed" '' lsfg-vk lsfg-vk before
expect_run "no choice, MAKO installed" '' mako mako before
expect_run "no choice, both installed" '' both lsfg-vk before

made="$work/case-9/.config/mako-render/conf.toml"
want=$'version = 2\n\n[global]\n\n[[profile]]\nname = "vkdaedalus default"\nactive_in = "/Game.exe"\nframe_generation_enabled = false\n'
if [ ! -f "$made" ] || [ "$(cat "$made"; printf x)" != "${want}x" ]; then
  fail "a game whose profile runs an installed MAKO did not get MAKO's profile for it, with frame generation off, before it started"
fi
for kept in 7 8 10; do
  [ ! -e "$work/case-$kept/.config/mako-render/conf.toml" ] || fail "case $kept wrote MAKO's file for a game whose profile runs lsfg-vk"
done
[ ! -e "$work/case-9/.config/lsfg-vk/conf.toml" ] || fail "a game whose profile runs MAKO wrote lsfg-vk's file"

if [ "$failures" -gt 0 ]; then
  echo "$failures of the checks on vkdaedalus-run's frame generation layer failed"
  exit 1
fi
echo "vkdaedalus-run loads only the profile's frame generation layer, in the profile's order, in $cases runs"
