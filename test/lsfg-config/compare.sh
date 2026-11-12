#!/usr/bin/env bash
set -euo pipefail

if [ $# -ne 4 ]; then
  echo "usage: compare.sh <vkDaedalus probe> <lsfg-vk probe> <test/lsfg-config folder> <work folder>" >&2
  exit 2
fi
ours=$1
theirs=$2
cases=$3
work=$4
failures=0
compared=0

for probe in "$ours" "$theirs"; do
  [ "$(basename "$probe")" = lsfg-config-probe ] || {
    echo "$probe is not named lsfg-config-probe, so the two probes do not match profiles by the same program name"
    exit 2
  }
done

fail() {
  echo "$1"
  failures=$((failures + 1))
}

probe() {
  local program=$1 out=$2
  shift 2
  local environment=()
  while [ "$1" != -- ]; do
    environment+=("$1")
    shift
  done
  shift
  env -i "${environment[@]}" "$program" "$@" >"$out" || {
    echo "$program $* exited with $?"
    return 1
  }
}

agree() {
  local label=$1
  shift
  compared=$((compared + 1))
  if ! { probe "$ours" "$work/ours.out" "$@" && probe "$theirs" "$work/theirs.out" "$@" &&
    diff -u --label "vkDaedalus: $label" --label "lsfg-vk: $label" "$work/ours.out" "$work/theirs.out"; }; then
    fail "vkDaedalus and lsfg-vk disagree: $label"
  fi
}

readable() {
  local file=$1 name=$2
  agree "read $name" -- read "$file"
  probe "$theirs" "$work/verdict" -- read "$file" || {
    fail "lsfg-vk could not read $name"
    return
  }
  [ "$(cat "$work/verdict")" != refused ] || return 0
  if probe "$theirs" /dev/null -- rewrite "$file" "$work/rewritten.toml"; then
    agree "read lsfg-vk's own rewrite of $name" -- read "$work/rewritten.toml"
  else
    fail "lsfg-vk could not rewrite $name"
  fi
}

agree "the names of every choice" -- names

for seed in /dev/null "$cases/files/seed.toml"; do
  edits="$work/edits-$(basename "$seed")"
  mkdir "$edits"
  "$ours" write "$seed" "$edits" || fail "the Frame generation editor could not write its edits on $seed"
  written=0
  for file in "$edits"/*.toml; do
    [ -e "$file" ] || continue
    written=$((written + 1))
    readable "$file" "edit $(basename "$file") on $(basename "$seed")"
  done
  [ "$written" -gt 0 ] || fail "the Frame generation editor wrote no edit on $seed"
done

files=0
for file in "$cases"/files/*.toml; do
  files=$((files + 1))
  readable "$file" "$(basename "$file")"
done
[ "$files" -gt 0 ] || fail "no file in $cases/files"

printf '\357\273\277' >"$work/byte-order-mark.toml"
cat "$cases/files/seed.toml" >>"$work/byte-order-mark.toml"
sed 's/$/\r/' "$cases/files/seed.toml" >"$work/crlf.toml"
printf 'version = 2\n[global]\n[[profile]]\nname = "\377"\n' >"$work/not-utf-8.toml"
printf 'version = 2\n[global]\n[[profile]]\nname = "a\001b"\n' >"$work/control-character.toml"
for file in byte-order-mark crlf not-utf-8 control-character; do
  readable "$work/$file.toml" "$file.toml"
done

agree "config path with nothing set" -- path
agree "config path with HOME" HOME=/home/a -- path
agree "config path with an empty HOME" HOME= -- path
agree "config path with XDG_CONFIG_HOME" HOME=/home/a XDG_CONFIG_HOME=/config -- path
agree "config path with an empty XDG_CONFIG_HOME" HOME=/home/a XDG_CONFIG_HOME= -- path
agree "config path in a Flatpak" HOME=/home/a XDG_CONFIG_HOME=/config LSFGVK_FLATPAK=1 -- path
agree "config path with an empty LSFGVK_FLATPAK" HOME=/home/a XDG_CONFIG_HOME=/config LSFGVK_FLATPAK= -- path
agree "config path named" HOME=/home/a XDG_CONFIG_HOME=/config LSFGVK_CONFIG=/named/conf.toml -- path
agree "config path named empty" HOME=/home/a LSFGVK_CONFIG= -- path

mkdir -p "$work/maps"
printf x >"$work/maps/Game.exe"
printf x >"$work/maps/Other.EXE"
identify=$cases/identify
agree "profile chosen by the executable" -- identify "$identify/every-method.toml"
agree "profile chosen by LSFGVK_PROFILE" LSFGVK_PROFILE=named -- identify "$identify/every-method.toml"
agree "profile LSFGVK_PROFILE names no profile" LSFGVK_PROFILE=missing -- identify "$identify/every-method.toml"
agree "profile with an empty LSFGVK_PROFILE" LSFGVK_PROFILE= -- identify "$identify/every-method.toml"
agree "profile chosen by a mapped .exe" -- identify "$identify/no-executable.toml" "$work/maps/Game.exe"
agree "profile with a mapped .EXE" -- identify "$identify/no-executable.toml" "$work/maps/Other.EXE"
agree "profile chosen by the process name" -- identify "$identify/no-executable.toml"
agree "profile chosen by SteamAppId" SteamAppId=4242 -- identify "$identify/steam-or-wine.toml"
agree "profile with an empty SteamAppId" SteamAppId= -- identify "$identify/steam-or-wine.toml"
agree "profile with another SteamAppId" SteamAppId=1 -- identify "$identify/steam-or-wine.toml"
agree "profile matched first" -- identify "$identify/first-match-wins.toml"
agree "profile matching the full program name and a repeated name" SteamAppId=4242 LSFGVK_PROFILE="same name" -- identify "$identify/names-and-duplicates.toml"

home=$work/home
library="$home/.local/share/Steam/steamapps/common/Lossless Scaling"
data=$work/data
mkdir -p "$library" "$data/.steam/steam/steamapps/common/Lossless Scaling" "$work/cwd-file" "$work/cwd-folder/lsfg-vk.dll" "$work/empty"
printf 'version = 2\n[global]\n[[profile]]\nname = "a"\n' >"$work/no-dll.toml"
dll_config() {
  printf 'version = 2\n[global]\ndll = "%s"\n[[profile]]\nname = "a"\n' "$1" >"$work/$2.toml"
}
printf 'version = 2\n[global]\ndll = "~/games/Lossless Scaling"\n[[profile]]\nname = "a"\n' >"$work/home-folder.toml"
dll_config /games/Lossless.dll lossless
dll_config /games/LosslessScaling.dll lossless-scaling
dll_config "/games/Lossless Scaling/" folder-slash
dll_config /games/own.dll own
agree "DLL found nowhere" -C "$work/empty" HOME="$work/empty" LSFGVK_CONFIG="$work/no-dll.toml" -- dll
printf x >"$library/lsfg-vk.dll"
agree "DLL in the home Steam library" -C "$work/empty" HOME="$home" LSFGVK_CONFIG="$work/no-dll.toml" -- dll
printf x >"$data/.steam/steam/steamapps/common/Lossless Scaling/lsfg-vk.dll"
agree "DLL under XDG_DATA_HOME" -C "$work/empty" HOME="$home" XDG_DATA_HOME="$data" LSFGVK_CONFIG="$work/no-dll.toml" -- dll
printf x >"$work/cwd-file/lsfg-vk.dll"
agree "DLL in the working folder" -C "$work/cwd-file" HOME="$work/empty" LSFGVK_CONFIG="$work/no-dll.toml" -- dll
agree "DLL path that is a folder" -C "$work/cwd-folder" HOME="$work/empty" LSFGVK_CONFIG="$work/no-dll.toml" -- dll
for config in home-folder lossless lossless-scaling folder-slash own; do
  agree "DLL named in the file: $config" -C "$work/empty" HOME="$home" LSFGVK_CONFIG="$work/$config.toml" -- dll
done
agree "DLL named by LSFGVK_DLL_PATH" -C "$work/empty" HOME="$home" LSFGVK_CONFIG="$work/own.toml" LSFGVK_DLL_PATH=/elsewhere/LosslessScaling.dll -- dll
agree "DLL with an empty LSFGVK_DLL_PATH" -C "$work/empty" HOME="$home" LSFGVK_CONFIG="$work/own.toml" LSFGVK_DLL_PATH= -- dll

if [ "$failures" -gt 0 ]; then
  echo "$failures comparisons between vkDaedalus' Frame generation file handling and lsfg-vk's own failed"
  exit 1
fi
echo "vkDaedalus reads, writes and resolves lsfg-vk's file as lsfg-vk does, in $compared comparisons"
