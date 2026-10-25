#!/usr/bin/env bash
set -uo pipefail

ROOTS=("$@")
[ "${#ROOTS[@]}" -gt 0 ] || ROOTS=(src tools)
SRC="${ROOTS[0]}"

fail=0
while read -r header; do
    case "$header" in
        */imgui/*|*/reshade/*) continue ;;
    esac

    base=$(basename "$header")
    own="${header%.hpp}.cpp"
    consumers=$(grep -rlE "#include \"[^\"]*${base}\"" "${ROOTS[@]}" --include='*.cpp' --include='*.hpp' --include='*.c')
    status=$?
    if [ "$status" -gt 1 ]; then
        echo "grep failed with status $status while looking for includers of $header"
        exit 2
    fi
    referenced=0
    while read -r consumer; do
        [ -z "$consumer" ] && continue
        [ "$consumer" = "$header" ] && continue
        [ "$consumer" = "$own" ] && continue
        referenced=1
        break
    done <<<"$consumers"

    if [ "$referenced" -eq 0 ]; then
        echo "  dormant  $header is included by nothing but its own source"
        fail=1
    fi
done < <(find "$SRC" -name '*.hpp' -type f | sort)

if [ "$fail" -ne 0 ]; then
    echo "a module compiled into the layer must have a caller, or be deleted"
    exit 1
fi

echo "no dormant modules"
