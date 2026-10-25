root=$(git rev-parse --show-toplevel)
lock="$root/test/shader-corpus.lock.json"
export GIT_TERMINAL_PROMPT=0

index_rev=$(git ls-remote https://github.com/crosire/reshade-shaders refs/heads/list | cut -f1)
[ -n "$index_rev" ] || { echo "crosire/reshade-shaders has no list branch" >&2; exit 1; }
index=$(nix flake prefetch --json "github:crosire/reshade-shaders/$index_rev")
index_path=$(jq -r .storePath <<<"$index")
index_hash=$(jq -r .hash <<<"$index")
[ -f "$index_path/EffectPackages.ini" ] || { echo "the index at $index_rev has no EffectPackages.ini" >&2; exit 1; }

mapfile -t urls < <(grep -oE 'https://github.com/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+' "$index_path/EffectPackages.ini" | sed 's/\.git$//' | sort -u)
[ "${#urls[@]}" -gt 0 ] || { echo "EffectPackages.ini at $index_rev lists no GitHub repository" >&2; exit 1; }

entries=()
for url in "${urls[@]}"; do
    path=${url#https://github.com/}
    owner=${path%%/*}
    repo=${path#*/}
    rev=$(git ls-remote "$url" HEAD | cut -f1)
    [ -n "$rev" ] || { echo "$owner/$repo: no default branch to pin" >&2; exit 1; }
    hash=$(nix flake prefetch --json "github:$owner/$repo/$rev" | jq -r .hash)
    [ -n "$hash" ] || { echo "$owner/$repo: no hash for $rev" >&2; exit 1; }
    entries+=("$(jq -cn --arg owner "$owner" --arg repo "$repo" --arg rev "$rev" --arg hash "$hash" \
        '{owner: $owner, repo: $repo, rev: $rev, hash: $hash}')")
    echo "$owner/$repo at $rev" >&2
done

printf '%s\n' "${entries[@]}" | jq -s --arg rev "$index_rev" --arg hash "$index_hash" \
    '{index: {owner: "crosire", repo: "reshade-shaders", rev: $rev, hash: $hash},
      packs: sort_by((.owner | ascii_downcase), (.repo | ascii_downcase))}' >"$lock.tmp"
mv "$lock.tmp" "$lock"
echo "pinned ${#entries[@]} packs from the index at $index_rev in $lock" >&2
