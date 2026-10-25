#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C

[ "$#" -gt 0 ] || { echo "usage: shader-corpus.sh <shader pack directory>..." >&2; exit 2; }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

for pack in "$@"; do
    find "$pack" -type f -name '*.fx' -printf '%h\n'
done | sort -u >"$work/dirs.txt"

for pack in "$@"; do
    find "$pack" -type d -iname 'Shaders' -exec find {} -type d \;
done | sort -u >"$work/include-dirs.txt"

mapfile -t dirs <"$work/dirs.txt"
[ "${#dirs[@]}" -gt 0 ] || { echo "no .fx file in the $# pack(s) given" >&2; exit 1; }
include_args=()
while read -r dir; do include_args+=(--include "$dir"); done <"$work/include-dirs.txt"
echo "packs: $#, shader dirs: ${#dirs[@]}, include dirs: $(wc -l <"$work/include-dirs.txt")" >&2

mkdir -p "$work/spv" "$work/cache"
tester_status=0
XDG_CACHE_HOME="$work/cache" vkdaedalus-test-shaders --dump-spirv "$work/spv" "${include_args[@]}" "${dirs[@]}" \
    >"$work/report.txt" 2>&1 || tester_status=$?
if ! grep -qE '^  RESULTS:' "$work/report.txt"; then
    cat "$work/report.txt" >&2
    echo "the tester stopped before reporting results, status $tester_status" >&2
    exit 1
fi
grep -E '^  RESULTS:' "$work/report.txt" >&2

grep -E '^(PASS|WARN|FAIL) ' "$work/report.txt" | awk '{ print $1, $2 }' | sort >"$work/raw-verdicts.txt"

: >"$work/invalid.txt"
modules=0
while IFS= read -r -d '' spv; do
    modules=$((modules + 1))
    if err=$(spirv-val --target-env vulkan1.1 "$spv" 2>&1); then continue; fi
    if grep -q 'No OpEntryPoint instruction was found' <<<"$err"; then continue; fi
    name=$(basename "$spv" .spv)
    printf '%s\n' "${name%__*}" >>"$work/invalid.txt"
done < <(find "$work/spv" -name '*.spv' -print0)
sort -u -o "$work/invalid.txt" "$work/invalid.txt"
echo "SPIR-V: $(wc -l <"$work/invalid.txt") of $modules emitted modules are invalid" >&2

awk 'FILENAME == ARGV[1] { bad[$1] = 1; next } { print ($2 in bad) ? "INVALID " $2 : $0 }' \
    "$work/invalid.txt" "$work/raw-verdicts.txt" | sort
