#!/usr/bin/env bash
set -u

usage="usage: study collect <live log>... | study write <data.tsv> <doc.md> | study check <data.tsv> <doc.md>"
begin='<!-- BEGIN generated:study -->'
end='<!-- END generated:study -->'

collect() {
    awk '
        BEGIN {
            n = split("per-frame cost, one effect, [a-z0-9]+;per-frame cost of the HUD, [a-z0-9]+;per-frame cost of the launch notice, [a-z0-9]+;per-frame cost of half precision, [a-z0-9]+;per-frame cost of instant ReShade sliders, [a-z0-9]+;per-frame cost of none in this run;memory of one effect image;queue drain vs own fence;lsfg-vk switched live, effects (before|after): gaps between presents (above|below) lsfg-vk;lsfg-vk switched live, effects (before|after): (base|off|on|three|file-off)", known, ";")
        }
        function last(text, part,    at, found) {
            found = 0
            while ((at = index(substr(text, found + 1), part)) > 0) found += at
            return found
        }
        /^=== / {
            header = substr($0, 5)
            sub(/ \[[a-z]+\] ===$/, "", header)
            cut = last(header, " (")
            if (cut > 0) header = substr(header, 1, cut - 1)
            comma = last(header, ", ")
            gpu = comma > 0 ? substr(header, 1, comma - 1) : header
            driver = comma > 0 ? substr(header, comma + 2) : ""
            next
        }
        /^  MEAS / {
            line = substr($0, 8)
            sub(/^ +/, "", line)
            for (i = 1; i <= n; i++) {
                if (match(line, "^(" known[i] ")")) {
                    name = substr(line, 1, RLENGTH)
                    figure = substr(line, RLENGTH + 1)
                    sub(/^ +/, "", figure)
                    gsub(/\t/, " ", figure)
                    key = gpu "\t" name
                    if (!(key in seen)) {
                        seen[key] = 1
                        printf "%s\t%s\t%s\t%s\n", gpu, driver, name, figure
                    }
                    break
                }
            }
        }
    ' "$@"
}

render() {
    awk -F'\t' '
        NF < 4 { next }
        {
            if (!($1 in order)) { order[$1] = ++gpus; title[gpus] = $1; drv[gpus] = $2 }
            g = order[$1]
            rows[g] = rows[g] sprintf("| %s | %s |\n", $3, $4)
        }
        END {
            for (g = 1; g <= gpus; g++) {
                printf "### %s, driver %s\n\n| Measured | Result |\n| --- | --- |\n%s", title[g], drv[g] == "" ? "unknown" : drv[g], rows[g]
                if (g < gpus) printf "\n"
            }
        }
    ' "$1"
}

block_of() {
    awk -v b="$begin" -v e="$end" '$0 == b { on = 1; next } $0 == e { on = 0 } on' "$1"
}

case "${1:-}" in
    collect)
        shift
        [ "$#" -gt 0 ] || { echo "$usage" >&2; exit 2; }
        collect "$@"
        ;;
    write)
        [ "$#" -eq 3 ] || { echo "$usage" >&2; exit 2; }
        if ! grep -qxF "$begin" "$3" || ! grep -qxF "$end" "$3"; then
            echo "$3 has no $begin and $end lines" >&2
            exit 1
        fi
        rendered=$(render "$2")
        [ -n "$rendered" ] || { echo "$2 holds no measurement" >&2; exit 1; }
        awk -v b="$begin" -v e="$end" -v r="$rendered" '$0 == b { print; print ""; print r; print ""; skip = 1; next } $0 == e { skip = 0 } !skip' "$3" >"$3.new" && mv "$3.new" "$3"
        ;;
    check)
        [ "$#" -eq 3 ] || { echo "$usage" >&2; exit 2; }
        want=$(printf '\n%s\n' "$(render "$2")")
        have=$(block_of "$3")
        if [ "$want" != "$(printf '%s\n' "$have")" ]; then
            echo "$3's measured tables differ from $2: run study write $2 $3"
            diff <(printf '%s\n' "$want") <(printf '%s\n' "$have") | head -20
            exit 1
        fi
        ;;
    *)
        echo "$usage" >&2
        exit 2
        ;;
esac
