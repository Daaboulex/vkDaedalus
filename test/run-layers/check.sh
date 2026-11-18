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
counter=VK_LAYER_VKDAEDALUS_present_counter
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

names() {
  awk -v name="$1" 'index($0, name "=") == 1 { c++ } END { print c + 0 }' "$2"
}

expect_run() {
  local label=$1 key=$2 layers=$3 want_layer=$4 want_order=$5 want_record=$6
  shift 6
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
    if [ "$want_order" = before ]; then want="$ours $mako $counter"; fi
    [ "$(value_of ENABLE_MAKO "$home/environment")" = 1 ] || fail "$label: ENABLE_MAKO is not 1, so MAKO stays out of a game whose profile runs it"
    [ "$(value_of DISABLE_LSFGVK "$home/environment")" = 1 ] || fail "$label: DISABLE_LSFGVK is not 1, so lsfg-vk joins a game whose profile runs MAKO"
    [ -z "$(value_of VKDAEDALUS_AFTER_LSFGVK "$home/environment")" ] || fail "$label: VKDAEDALUS_AFTER_LSFGVK is set for a profile running MAKO"
  else
    want="$lsfg $ours"
    if [ "$want_order" = before ]; then want="$ours $lsfg $counter"; fi
    [ "$(value_of DISABLE_MAKO "$home/environment")" = 1 ] || fail "$label: DISABLE_MAKO is not 1, so MAKO joins a game whose profile runs lsfg-vk"
    [ -z "$(value_of ENABLE_MAKO "$home/environment")" ] || fail "$label: ENABLE_MAKO is still set for a profile running lsfg-vk"
    [ -z "$(value_of VKDAEDALUS_AFTER_MAKO "$home/environment")" ] || fail "$label: VKDAEDALUS_AFTER_MAKO is set for a profile running lsfg-vk"
  fi
  local own=DISABLE_LSFGVK kept=0
  if [ "$want_layer" = mako ]; then own=DISABLE_MAKO; fi
  if [[ " $want_record " == *" $own "* ]]; then kept=1; fi
  [ "$(names "$own" "$home/environment")" = "$kept" ] ||
    fail "$label: $own is $( [ "$kept" = 1 ] && echo unset || echo set), but only the launch options may keep out the layer the profile runs"
  [ "$(value_of VKDAEDALUS_RUN_FRAME_GENERATION_LAYER "$home/environment")" = "$want_layer" ] ||
    fail "$label: VKDAEDALUS_RUN_FRAME_GENERATION_LAYER is '$(value_of VKDAEDALUS_RUN_FRAME_GENERATION_LAYER "$home/environment")', so the overlay cannot tell a layer kept out for the other from one the launch options disable"
  [ "$(names VKDAEDALUS_RUN_CALLER_DISABLED "$home/environment")" = 1 ] && [ "$(value_of VKDAEDALUS_RUN_CALLER_DISABLED "$home/environment")" = "$want_record" ] ||
    fail "$label: VKDAEDALUS_RUN_CALLER_DISABLED is '$(value_of VKDAEDALUS_RUN_CALLER_DISABLED "$home/environment")' ($(names VKDAEDALUS_RUN_CALLER_DISABLED "$home/environment") set), want exactly '$want_record', so the overlay takes a DISABLE variable vkdaedalus-run set for one the launch options set, or the other way round"
  [ "$on" = "$want" ] || fail "$label: the order layers on are '${on//$'\n'/ | }', want exactly '$want'"
  [ "$(value_of VK_ADD_LAYER_PATH "$home/environment" | cut -d: -f1)" = "$(dirname "$orders")/counter-layer" ] && [ -f "$(dirname "$orders")/counter-layer/vkdaedalus_present_counter.json" ] ||
    fail "$label: VK_ADD_LAYER_PATH is '$(value_of VK_ADD_LAYER_PATH "$home/environment")', not led by the present counter's folder holding its manifest, so the loader drops the order layers that name it"
}

expect_run "MAKO, effects before" $'frameGenerationLayer = mako\n' none mako before ''
expect_run "MAKO, effects after" $'frameGenerationLayer = mako\neffectsAfterFrameGeneration = true\n' none mako after ''
expect_run "lsfg-vk, effects before" $'frameGenerationLayer = lsfg-vk\n' none lsfg-vk before ''
expect_run "lsfg-vk, effects after" $'frameGenerationLayer = lsfg-vk\neffectsAfterFrameGeneration = true\n' none lsfg-vk after ''
expect_run "lsfg-vk, started with MAKO's variables and both orders set" $'frameGenerationLayer = lsfg-vk\n' none lsfg-vk before ENABLE_MAKO \
  ENABLE_MAKO=1 VKDAEDALUS_AFTER_MAKO=1 VKDAEDALUS_AFTER_LSFGVK=1
expect_run "MAKO, started with lsfg-vk's after order set" $'frameGenerationLayer = mako\n' none mako before '' VKDAEDALUS_AFTER_LSFGVK=1 VKDAEDALUS_RUN_FRAME_GENERATION_LAYER=lsfg-vk
expect_run "no choice, nothing installed" '' none lsfg-vk before ''
expect_run "no choice, lsfg-vk installed" '' lsfg-vk lsfg-vk before ''
expect_run "no choice, MAKO installed" '' mako mako before ''
expect_run "no choice, both installed" '' both lsfg-vk before ''
expect_run "lsfg-vk, the launch options keep MAKO out" $'frameGenerationLayer = lsfg-vk\n' both lsfg-vk before DISABLE_MAKO DISABLE_MAKO=1
expect_run "MAKO, the launch options keep MAKO out" $'frameGenerationLayer = mako\n' both mako before DISABLE_MAKO DISABLE_MAKO=1
expect_run "MAKO, the launch options keep lsfg-vk out" $'frameGenerationLayer = mako\neffectsAfterFrameGeneration = true\n' both mako after DISABLE_LSFGVK DISABLE_LSFGVK=
expect_run "lsfg-vk, the launch options keep both out and name a record of their own" $'frameGenerationLayer = lsfg-vk\n' both lsfg-vk before 'DISABLE_LSFGVK DISABLE_MAKO' \
  DISABLE_MAKO=1 DISABLE_LSFGVK=1 VKDAEDALUS_RUN_CALLER_DISABLED=DISABLE_LSFGVK

started_by() {
  grep -E '^(VKDAEDALUS_RUN|VKDAEDALUS_RUN_CALLER_DISABLED|VKDAEDALUS_RUN_FRAME_GENERATION_LAYER|VKDAEDALUS_AFTER_LSFGVK|VKDAEDALUS_AFTER_MAKO|ENABLE_MAKO|DISABLE_LSFGVK|DISABLE_MAKO)=' "$work/case-$1/environment"
}
mapfile -t outer < <(started_by 3)
expect_run "MAKO, started by a launcher vkdaedalus-run started for lsfg-vk" $'frameGenerationLayer = mako\n' both mako before '' "${outer[@]}"
mapfile -t outer < <(started_by 1)
expect_run "lsfg-vk, started by a launcher vkdaedalus-run started for MAKO" $'frameGenerationLayer = lsfg-vk\n' both lsfg-vk before '' "${outer[@]}"
mapfile -t outer < <(started_by 12)
expect_run "MAKO, started by a launcher whose launch options keep MAKO out" $'frameGenerationLayer = mako\n' both mako before DISABLE_MAKO "${outer[@]}"
expect_run "no choice, both installed, the launch options enable MAKO" '' both mako before ENABLE_MAKO ENABLE_MAKO=1
expect_run "no choice, lsfg-vk alone installed, the launch options enable MAKO" '' lsfg-vk lsfg-vk before ENABLE_MAKO ENABLE_MAKO=1
expect_run "no choice, both installed, the launch options enable MAKO and keep it out" '' both lsfg-vk before 'DISABLE_MAKO ENABLE_MAKO' ENABLE_MAKO=1 DISABLE_MAKO=1
expect_run "lsfg-vk, both installed, the launch options enable MAKO" $'frameGenerationLayer = lsfg-vk\n' both lsfg-vk before ENABLE_MAKO ENABLE_MAKO=1
mapfile -t outer < <(started_by 1)
expect_run "no choice, started by a launcher vkdaedalus-run started for MAKO" '' both lsfg-vk before '' "${outer[@]}"
mapfile -t outer < <(started_by 21)
expect_run "no choice, started by a launcher run for lsfg-vk whose launch options enable MAKO" '' both mako before ENABLE_MAKO "${outer[@]}"
mapfile -t outer < <(started_by 18)
expect_run "no choice, started by a launcher run for MAKO whose launch options enable MAKO" '' both mako before ENABLE_MAKO "${outer[@]}"

made="$work/case-9/.config/mako-render/conf.toml"
want=$'version = 2\n\n[global]\n\n[[profile]]\nname = "vkdaedalus default"\nactive_in = "/Game.exe"\nframe_generation_enabled = false\n'
if [ ! -f "$made" ] || [ "$(cat "$made"; printf x)" != "${want}x" ]; then
  fail "a game whose profile runs an installed MAKO did not get MAKO's profile for it, with frame generation off, before it started"
fi
for kept in 7 8 10; do
  [ ! -e "$work/case-$kept/.config/mako-render/conf.toml" ] || fail "case $kept wrote MAKO's file for a game whose profile runs lsfg-vk"
done
[ ! -e "$work/case-9/.config/lsfg-vk/conf.toml" ] || fail "a game whose profile runs MAKO wrote lsfg-vk's file"
[ ! -e "$work/case-12/.config/mako-render/conf.toml" ] || fail "a game whose launch options keep MAKO out got MAKO's profile written for it"

if [ "$failures" -gt 0 ]; then
  echo "$failures of the checks on vkdaedalus-run's frame generation layer failed"
  exit 1
fi
echo "vkdaedalus-run loads only the profile's frame generation layer, in the profile's order, in $cases runs"
