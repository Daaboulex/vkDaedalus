#!/usr/bin/env bash
set -euo pipefail

if [ $# -ne 4 ]; then
  echo "usage: compare.sh <vkDaedalus probe> <MAKO probe> <test/mako-config folder> <work folder>" >&2
  exit 2
fi
ours=$1
theirs=$2
cases=$3
work=$4
failures=0
compared=0

for program in "$ours" "$theirs"; do
  [ "$(basename "$program")" = mako-config-probe ] || {
    echo "$program is not named mako-config-probe, so the two probes do not match profiles by the same program name"
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

toml_library_words_set_aside() {
  awk 'after_parse_refusal && index($0, "- ") == 1 { print "- the TOML library'\''s own words"; after_parse_refusal = 0; next }
       { after_parse_refusal = ($0 == "unable to parse configuration"); print }' "$1" >"$1.compared"
}

agree_between() {
  local mine=$1 makos=$2 label=$3
  shift 3
  compared=$((compared + 1))
  if ! { probe "$mine" "$work/ours.out" "$@" && probe "$makos" "$work/theirs.out" "$@" &&
    toml_library_words_set_aside "$work/ours.out" && toml_library_words_set_aside "$work/theirs.out" &&
    diff -u --label "vkDaedalus: $label" --label "MAKO: $label" "$work/ours.out.compared" "$work/theirs.out.compared"; }; then
    fail "vkDaedalus and MAKO disagree: $label"
  fi
}

agree() {
  agree_between "$ours" "$theirs" "$@"
}

readable() {
  local file=$1 name=$2
  agree "read $name" -- read "$file"
  probe "$theirs" "$work/verdict" -- read "$file" || {
    fail "MAKO could not read $name"
    return
  }
  [ "$(head -n 1 "$work/verdict")" != refused ] || return 0
  for source in handheld docked unknown; do
    agree "read $name as $source" -- read-as "$file" "$source"
  done
  if probe "$theirs" /dev/null -- rewrite "$file" "$work/rewritten.toml"; then
    agree "read MAKO's own rewrite of $name" -- read "$work/rewritten.toml"
  else
    fail "MAKO could not rewrite $name"
  fi
}

agree "the names of every choice" -- names
agree "the limits of every value" -- limits

for seed in /dev/null "$cases/files/seed.toml"; do
  edits="$work/edits-$(basename "$seed")"
  mkdir "$edits"
  "$ours" write "$seed" "$edits" || fail "the MAKO editor could not write its edits on $seed"
  written=0
  for file in "$edits"/*.toml; do
    [ -e "$file" ] || continue
    written=$((written + 1))
    readable "$file" "edit $(basename "$file") on $(basename "$seed")"
  done
  [ "$written" -gt 0 ] || fail "the MAKO editor wrote no edit on $seed"
done

files=0
refusals=0
for file in "$cases"/files/*.toml; do
  files=$((files + 1))
  readable "$file" "$(basename "$file")"
  probe "$theirs" "$work/verdict" -- read "$file" && [ "$(head -n 1 "$work/verdict")" = refused ] && refusals=$((refusals + 1))
done
[ "$files" -gt 0 ] || fail "no file in $cases/files"
[ "$refusals" -gt 0 ] || fail "no file in $cases/files is one MAKO refuses"

printf '\357\273\277' >"$work/byte-order-mark.toml"
cat "$cases/files/seed.toml" >>"$work/byte-order-mark.toml"
sed 's/$/\r/' "$cases/files/seed.toml" >"$work/crlf.toml"
printf 'version = 2\n[[profile]]\nname = "\377"\n' >"$work/not-utf-8.toml"
printf 'version = 2\n[[profile]]\nname = "a\001b"\n' >"$work/control-character.toml"
: >"$work/empty.toml"
for file in byte-order-mark crlf not-utf-8 control-character empty; do
  readable "$work/$file.toml" "$file.toml"
done

agree "config path with nothing set" -- path
agree "config path with HOME" HOME=/home/a -- path
agree "config path with an empty HOME" HOME= -- path
agree "config path with XDG_CONFIG_HOME" HOME=/home/a XDG_CONFIG_HOME=/config -- path
agree "config path with an empty XDG_CONFIG_HOME" HOME=/home/a XDG_CONFIG_HOME= -- path
agree "config path with a trailing slash" HOME=/home/a XDG_CONFIG_HOME=/config/ -- path
agree "config path named" HOME=/home/a XDG_CONFIG_HOME=/config MAKO_CONFIG=/named/conf.toml -- path
agree "config path named empty" HOME=/home/a MAKO_CONFIG= -- path

mkdir -p "$work/maps" "$work/wine/ours" "$work/wine/theirs"
printf x >"$work/maps/Game.exe"
printf x >"$work/maps/Other.EXE"
printf x >"$work/maps/UPC.EXE"
cp "$ours" "$work/wine/ours/mako-config-probe"
cp "$theirs" "$work/wine/theirs/mako-config-probe"
wine_ours=$work/wine/ours/mako-config-probe
wine_theirs=$work/wine/theirs/mako-config-probe
identify=$cases/identify
agree "profile chosen by the executable" -- identify "$identify/every-method.toml"
agree "profile chosen by MAKO_PROFILE" MAKO_PROFILE=named -- identify "$identify/every-method.toml"
agree "profile MAKO_PROFILE names no profile" MAKO_PROFILE=missing -- identify "$identify/every-method.toml"
agree "profile with an empty MAKO_PROFILE" MAKO_PROFILE= -- identify "$identify/every-method.toml"
agree "profile following current_profile" MAKO_FOLLOW_CURRENT_PROFILE=1 MAKO_PROFILE=named -- identify "$identify/every-method.toml"
agree "profile following current_profile only with 1" MAKO_FOLLOW_CURRENT_PROFILE=yes MAKO_PROFILE=named -- identify "$identify/every-method.toml"
agree "profile following a missing current_profile" MAKO_FOLLOW_CURRENT_PROFILE=1 -- identify "$identify/no-current-profile.toml"
agree "profile matched before the fallback" MAKO_PROFILE_FALLBACK=fallback -- identify "$identify/every-method.toml"
agree "profile chosen by the process name" -- identify "$identify/no-executable.toml"
agree "profile with a mapped .exe outside Wine" -- identify "$identify/no-executable.toml" "$work/maps/Game.exe"
agree_between "$wine_ours" "$wine_theirs" "profile chosen by a mapped .exe under Wine" -- identify "$identify/no-executable.toml" "$work/maps/Game.exe"
agree_between "$wine_ours" "$wine_theirs" "profile with a mapped .EXE under Wine" -- identify "$identify/no-executable.toml" "$work/maps/Other.EXE"
agree_between "$wine_ours" "$wine_theirs" "profile for a launcher MAKO leaves alone" MAKO_PROFILE=named -- identify "$identify/no-executable.toml" "$work/maps/UPC.EXE"
agree "profile chosen by MAKO_PROFILE_FALLBACK" MAKO_PROFILE_FALLBACK=fallback -- identify "$identify/fallback-only.toml"
agree "profile MAKO_PROFILE_FALLBACK names no profile" MAKO_PROFILE_FALLBACK=missing -- identify "$identify/fallback-only.toml"
agree "profile with an empty MAKO_PROFILE_FALLBACK" MAKO_PROFILE_FALLBACK= -- identify "$identify/fallback-only.toml"
agree "profile matched first" -- identify "$identify/first-match-wins.toml"
agree "profile with an empty entry" -- identify "$identify/empty-entry.toml"

home=$work/home
library="$home/.local/share/Steam/steamapps/common/Lossless Scaling"
data=$work/data
mkdir -p "$library" "$data/Steam/steamapps/common/Lossless Scaling" "$data/.steam/steam/steamapps/common/Lossless Scaling" \
  "$work/cwd-file" "$work/cwd-folder/Lossless.dll" "$work/empty"
printf 'version = 2\n' >"$work/no-dll.toml"
printf 'version = 2\n[global]\ndll = "/games/Lossless.dll"\n' >"$work/named.toml"
printf 'version = 2\n[global]\ndll = "~/games/Lossless Scaling"\n' >"$work/home-folder.toml"
agree "DLL found nowhere" -C "$work/empty" HOME="$work/empty" MAKO_CONFIG="$work/no-dll.toml" -- dll
printf x >"$library/Lossless.dll"
agree "DLL in the home Steam library" -C "$work/empty" HOME="$home" MAKO_CONFIG="$work/no-dll.toml" -- dll
printf x >"$data/.steam/steam/steamapps/common/Lossless Scaling/Lossless.dll"
agree "DLL under XDG_DATA_HOME where MAKO does not look" -C "$work/empty" HOME="$work/empty" XDG_DATA_HOME="$data" MAKO_CONFIG="$work/no-dll.toml" -- dll
printf x >"$data/Steam/steamapps/common/Lossless Scaling/Lossless.dll"
agree "DLL under XDG_DATA_HOME" -C "$work/empty" HOME="$home" XDG_DATA_HOME="$data" MAKO_CONFIG="$work/no-dll.toml" -- dll
printf x >"$work/cwd-file/Lossless.dll"
agree "DLL in the working folder" -C "$work/cwd-file" HOME="$work/empty" MAKO_CONFIG="$work/no-dll.toml" -- dll
agree "DLL path that is a folder" -C "$work/cwd-folder" HOME="$work/empty" MAKO_CONFIG="$work/no-dll.toml" -- dll
agree "DLL named in the file" -C "$work/empty" HOME="$home" MAKO_CONFIG="$work/named.toml" -- dll
agree "DLL named with a home folder" -C "$work/empty" HOME="$home" MAKO_CONFIG="$work/home-folder.toml" -- dll
agree "DLL with MAKO_DLL_PATH, which only MAKO_ENV reads" -C "$work/empty" HOME="$home" MAKO_CONFIG="$work/no-dll.toml" MAKO_DLL_PATH=/elsewhere/Lossless.dll -- dll

power=$work/power
supply() {
  local tree=$1 entry=$2
  shift 2
  mkdir -p "$power/$tree/$entry"
  while [ $# -gt 0 ]; do
    printf '%s\n' "$2" >"$power/$tree/$entry/$1"
    shift 2
  done
}
supply plugged AC type Mains online 1
supply plugged BAT0 type Battery present 1
supply battery AC type Mains online 0
supply battery BAT0 type Battery present 1
mkdir -p "$power/desktop"
supply incomplete BAT0 type Battery present 1
supply incomplete ucsi-source-psy type USB
supply device AC type Mains online 0
supply device hidpp_battery_0 type Battery present 1 scope Device
supply untyped AC type Mains online 0
supply untyped BAT0 type Battery present 1
supply untyped odd
supply plugged-incomplete AC type Mains online 1
supply plugged-incomplete USB type USB_C
supply battery-absent AC type Mains online 0
supply battery-absent BAT0 type Battery present 0
supply wireless BAT0 type Battery present 1
supply wireless dock type Wireless online 1
supply no-present AC type Mains online 0
supply no-present BAT0 type Battery
for i in $(seq 0 64); do
  supply crowded "AC$i" type Mains online 0
done
supply crowded BAT0 type Battery present 1
for tree in plugged battery desktop missing incomplete device untyped plugged-incomplete battery-absent wireless no-present crowded; do
  agree "power source of $tree" -- power "$power/$tree"
done

if [ "$failures" -gt 0 ]; then
  echo "$failures comparisons between vkDaedalus' MAKO file handling and MAKO's own failed"
  exit 1
fi
echo "vkDaedalus reads, writes and resolves MAKO's file as MAKO does, in $compared comparisons"
