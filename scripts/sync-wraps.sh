root=$(git rev-parse --show-toplevel)
lock="$root/flake.lock"
[ -f "$lock" ] || { echo "no flake.lock at $root" >&2; exit 1; }

wraps=()
while IFS= read -r -d '' f; do wraps+=("$f"); done < <(find "$root/subprojects" -maxdepth 1 -name '*.wrap' -print0 | sort -z)
[ "${#wraps[@]}" -gt 0 ] || { echo "no subprojects/*.wrap in $root" >&2; exit 1; }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

for wrap in "${wraps[@]}"; do
    name=$(basename "$wrap" .wrap)
    if ! node=$(jq -e --arg n "$name" '.nodes[$n].locked' "$lock"); then
        echo "subprojects/$name.wrap has no flake input named $name in flake.lock" >&2
        exit 1
    fi
    type=$(jq -r .type <<<"$node")
    if [ "$type" != github ]; then
        echo "$name: flake.lock locks it as $type; only github inputs map to a wrap" >&2
        exit 1
    fi
    owner=$(jq -r .owner <<<"$node")
    repo=$(jq -r .repo <<<"$node")
    rev=$(jq -r .rev <<<"$node")
    url="https://github.com/$owner/$repo/archive/$rev.tar.gz"
    status=$(curl -sSL --retry 3 -o "$work/$name.tar.gz" -w '%{http_code}' "$url")
    if [ "$status" != 200 ]; then
        echo "$name: HTTP $status for $url" >&2
        exit 1
    fi
    hash=$(sha256sum "$work/$name.tar.gz" | cut -d' ' -f1)
    top=$(tar -tzf "$work/$name.tar.gz" | sed -n '1s|/.*||p')
    [ -n "$top" ] || { echo "$name: the archive at $url has no top directory" >&2; exit 1; }
    for key in directory source_url source_filename source_hash; do
        if [ "$(grep -c "^$key = " "$wrap")" != 1 ]; then
            echo "subprojects/$name.wrap must carry exactly one '$key = ' line for this updater to own" >&2
            exit 1
        fi
    done
    sed -i -E \
        -e "s|^directory = .*|directory = $top|" \
        -e "s|^source_url = .*|source_url = $url|" \
        -e "s|^source_filename = .*|source_filename = $repo-$rev.tar.gz|" \
        -e "s|^source_hash = .*|source_hash = $hash|" \
        "$wrap"
    echo "$name: $owner/$repo at $rev"
done
