set -euo pipefail
if [ "$#" -ne 2 ]; then
    echo "usage: build-live-helpers.sh <the repository's test folder> <output prefix>" >&2
    exit 2
fi
test=$1 out=$2
cc=${CC:-cc}
mkdir -p "$out/bin" "$out/lib" "$out/share/vulkan/implicit_layer.d" "$out/share/vulkan/explicit_layer.d"

"$cc" -O2 -o "$out/bin/queue-wait-bench" "$test/queue-wait/queue_wait_bench.c" -lvulkan
"$cc" -O2 -Wall -Werror -o "$out/bin/api-probe" "$test/api-probe/api_probe.c" -lvulkan
"$cc" -O2 -Wall -Werror -pthread -o "$out/bin/queue-race-probe" "$test/queue-race-probe/queue_race_probe.c" -lvulkan -lxcb
"$cc" -O2 -Wall -Werror -o "$out/bin/two-swapchain-probe" "$test/two-swapchain-probe/two_swapchain_probe.c" -lvulkan -lxcb
"$cc" -O2 -Wall -Werror -o "$out/bin/x11-input-probe" "$test/x11-input-probe/x11_input_probe.c" -lvulkan -lxcb
"$cc" -O2 -Wall -Werror -o "$out/bin/swapchain-probe" "$test/swapchain-probe/swapchain_probe.c" -lvulkan -lxcb
"$cc" -O2 -Wall -Werror -o "$out/bin/pixel-probe" "$test/pixel-probe/pixel_probe.c"
"$cc" -O2 -Wall -Werror -o "$out/bin/grab-probe" "$test/focus-loss/grab_probe.c" -lX11
"$cc" -O2 -Wall -Werror -o "$out/bin/multi-gpu-probe" "$test/multi-gpu-probe/multi_gpu_probe.c" -lvulkan
"$cc" -O2 -Wall -Werror -o "$out/bin/gpu-choice-probe" "$test/gpu-choice-probe/gpu_choice_probe.c" -lvulkan -lxcb

"$cc" -shared -fPIC -O2 -Wall -Werror -o "$out/lib/libvkdaedalus-test-absent.so" "$test/preload/absent.c"
"$cc" -shared -fPIC -O2 -Wall -Werror -o "$out/lib/libvkdaedalus-test-needs-absent.so" "$test/preload/needs_absent.c" \
    -L"$out/lib" -Wl,--no-as-needed -lvkdaedalus-test-absent
rm "$out/lib/libvkdaedalus-test-absent.so"

test_layer() {
    local library=$1 folder=$2 manifests=$3 name=$4 description=$5 extra=$6
    "$cc" -shared -fPIC -O2 -Wall -Werror -o "$out/lib/lib$library.so" "$test/$folder/$library.c" -ldl
    printf '{"file_format_version": "1.1.0", "layer": {"name": "%s", "type": "GLOBAL", "library_path": "%s", "api_version": "1.3.0", "implementation_version": "1", "description": "%s"%s}}\n' \
        "$name" "$out/lib/lib$library.so" "$description" "$extra" >"$out/share/vulkan/$manifests/$library.json"
}
test_layer mock_framegen mock-framegen implicit_layer.d VK_LAYER_MOCK_framegen "structural mock of a frame-generation layer" \
    ', "disable_environment": {"DISABLE_MOCK_FRAMEGEN": "1"}'
test_layer fault_inject fault-inject explicit_layer.d VK_LAYER_VKBASALT_TEST_fault_inject \
    "fails one named Vulkan call when the named library makes it" ''
