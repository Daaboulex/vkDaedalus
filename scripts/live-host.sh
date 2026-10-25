set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
die() {
    echo "live-host: $*" >&2
    exit 2
}
[ "$#" -ge 1 ] || die "usage: scripts/live-host.sh <meson build folder> [--keep-logs] [--parallel] [driver manifests]"
build=$(cd "$1" && pwd) || die "no build folder $1"
shift
library="$build/src/libvkdaedalus.so"
manifest="$build/config/vkdaedalus.json"
[ -f "$library" ] && [ -f "$manifest" ] || die "no built layer in $build; build it with: meson setup $build && ninja -C $build"

missing=()
for tool in cc vkcube vulkaninfo weston Xwayland xdotool gawk timeout setsid pgrep find sort mktemp; do
    command -v "$tool" >/dev/null 2>&1 || missing+=("$tool")
done
[ "${#missing[@]}" -eq 0 ] || die "this host lacks ${missing[*]}; the Vulkan headers and the xcb and X11 development files are needed too"
weston_help=$(weston --help 2>&1 || true)
case "$weston_help" in
    *vulkan*) ;;
    *) die "weston on this host has no Vulkan renderer, which the private sessions need (weston 14 or newer)" ;;
esac

find_layer_manifest() {
    local json=$1 dir
    local -a dirs
    IFS=: read -r -a dirs <<<"${XDG_DATA_HOME:-$HOME/.local/share}:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
    for dir in "${dirs[@]}" /etc; do
        if [ -f "$dir/vulkan/explicit_layer.d/$json" ]; then
            echo "$dir/vulkan/explicit_layer.d/$json"
            return 0
        fi
    done
    return 1
}
validation=$(find_layer_manifest VkLayer_khronos_validation.json) || die "no Khronos validation layer (VkLayer_khronos_validation.json) in any vulkan/explicit_layer.d"
screenshot=$(find_layer_manifest VkLayer_screenshot.json) || die "no LunarG screenshot layer (VkLayer_screenshot.json, from LunarG's VulkanTools) in any vulkan/explicit_layer.d"

mkdir -p "${XDG_CACHE_HOME:-$HOME/.cache}"
scratch=$(mktemp -d "${XDG_CACHE_HOME:-$HOME/.cache}/vkdaedalus-live-host.XXXXXX")
trap 'rm -rf "${scratch:?}"' EXIT
mkdir -p "$scratch/layer/lib" "$scratch/layer/share/vulkan/implicit_layer.d" \
    "$scratch/validation/share/vulkan/explicit_layer.d" "$scratch/screenshot/share/vulkan/explicit_layer.d"
cp "$library" "$scratch/layer/lib/libvkdaedalus.so"
sed -E "s|\"library_path\" *: *\"[^\"]*\"|\"library_path\": \"$scratch/layer/lib/libvkdaedalus.so\"|" "$manifest" \
    >"$scratch/layer/share/vulkan/implicit_layer.d/vkdaedalus.json"
copy_manifest() {
    local source=$1 target=$2 path
    path=$(sed -n -E 's/.*"library_path" *: *"([^"]*)".*/\1/p' "$source" | sed -n 1p)
    case "$path" in
        */*) [ "${path#/}" != "$path" ] || path="$(cd "$(dirname "$source")" && pwd)/$path" ;;
    esac
    sed -E "s|\"library_path\" *: *\"[^\"]*\"|\"library_path\": \"$path\"|" "$source" >"$target/$(basename "$source")"
}
copy_manifest "$validation" "$scratch/validation/share/vulkan/explicit_layer.d"
copy_manifest "$screenshot" "$scratch/screenshot/share/vulkan/explicit_layer.d"
bash "$repo/scripts/build-live-helpers.sh" "$repo/test" "$scratch/helpers"

rc=0
env VKB_HOST_TOOLS=1 VKB_LAYER="$scratch/layer" VKB_MOCK="$scratch/helpers" VKB_FAULT="$scratch/helpers" \
    VKB_VALIDATION="$scratch/validation" VKB_SCREENSHOT="$scratch/screenshot" VKB_FIXTURES="$repo/test" VKB_WSI_LIBRARIES="" \
    PATH="$scratch/helpers/bin:$PATH" bash -euo pipefail "$repo/scripts/live.sh" "$@" || rc=$?
exit "$rc"
