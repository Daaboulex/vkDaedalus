#!/usr/bin/env bash
set -u
[ "$#" -gt 0 ] || { echo "usage: docs-check.sh <markdown file>..."; exit 2; }
status=0
for file in "$@"; do
    [ -f "$file" ] || { echo "$file: not found"; status=1; continue; }
    found=$(LC_ALL=C awk -v file="$file" '
        function flag(rule) { printf "%s:%d: %s: %s\n", file, NR, rule, $0; bad = 1 }
        /^[ \t]*```/ { fenced = !fenced; next }
        fenced { next }
        /<!--.*-->/ { next }
        {
            prose = $0
            gsub(/`[^`]*`/, "", prose)
            gsub(/\]\([^)]*\)/, "]", prose)
            gsub(/<[^>]*>/, "", prose)
            lower = tolower(prose)
            if (prose ~ /[\200-\377]/) flag("a character outside plain ASCII (a dash, an arrow, a curly quote or an ellipsis character)")
            if (prose ~ /[^-]--[^-]/ || prose ~ /(^|[^-])--$/) flag("a double dash standing in for a dash")
            if (prose ~ /->|=>|<-/) flag("an arrow")
            if (prose ~ /\.\.\./) flag("an ellipsis")
            if (lower ~ /\[(click )?here\]/) flag("a link that says here")
            n = split("serves as|a testament|boasts|vibrant|delve|delves|crucial|underscore|underscores|seamless|seamlessly|robust|powerful|leverage|simply|just|easy|please", words, "|")
            for (i = 1; i <= n; i++)
                if (match(" " lower " ", "[^a-z]" words[i] "[^a-z]")) flag("the padded word \"" words[i] "\"")
            if ($0 ~ /^#+ /) {
                heading = $0
                sub(/^#+ /, "", heading)
                count = split(heading, part, " ")
                upper = 0
                lowered = 0
                for (i = 2; i <= count; i++) {
                    if (length(part[i]) < 4) continue
                    if (part[i] ~ /^[A-Z][a-z]/) upper++
                    else if (part[i] ~ /^[a-z]/) lowered++
                }
                if (upper >= 2 && lowered == 0) flag("a heading in title case")
            }
        }
        END { exit bad }' "$file") || status=1
    [ -z "$found" ] || printf '%s\n' "$found"
done
exit "$status"
