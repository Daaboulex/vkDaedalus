if [ -z "${VKB_CLEAN:-}" ]; then
    kept=(VKB_CLEAN=1 PATH="$PATH" HOME="$HOME")
    for name in TMPDIR XDG_CONFIG_HOME XDG_DATA_HOME XDG_CACHE_HOME "${!VKB_@}"; do
        if [ -n "${!name+set}" ]; then kept+=("$name=${!name}"); fi
    done
    echo "the runs start from an empty environment: $(($(env | grep -c '^[A-Za-z_][A-Za-z0-9_]*=') - ${#kept[@]} + 1)) variables of this session are left out"
    exec env -i "${kept[@]}" "$BASH" -euo pipefail "$0" "$@"
fi

BASALT=VK_LAYER_VKDAEDALUS_post_processing
MOCK=VK_LAYER_MOCK_framegen
LSFG=VK_LAYER_LSFGVK_frame_generation
VALIDATION=VK_LAYER_KHRONOS_validation
FRAMES=300
PERF_FRAMES=600
HANG_SECONDS=300
LOG_LEVEL=trace
OVERLAY_KEY=F9
TOGGLE_KEY=F10
RELOAD_KEY=F11
CONTROLS_FRAMES=900
UI_FRAMES=7200
CONFIG_FRAMES=120
LAYER_FRAMES=150
FAULT=VK_LAYER_VKBASALT_TEST_fault_inject
SCREENSHOT=VK_LAYER_LUNARG_screenshot
FAULT_FRAMES=90
FAULT_CALLS=(vkCreateImage vkCreateImageView vkCreateBuffer vkAllocateMemory vkCreateSampler vkCreateDescriptorPool
    vkCreateDescriptorSetLayout vkCreatePipelineLayout vkCreateShaderModule vkCreateRenderPass vkCreateFramebuffer
    vkCreateCommandPool vkCreateSemaphore vkCreateFence vkBindImageMemory vkBindBufferMemory vkAllocateDescriptorSets
    vkAllocateCommandBuffers vkBeginCommandBuffer vkEndCommandBuffer vkCreateGraphicsPipelines vkCreateComputePipelines vkMapMemory)
declare -A FAULT_REPORTS=([vkMapMemory]='mapping the uniform buffer for image')
LAYER_LIBRARY=$(sed -n -E 's/.*"library_path": *"[^"]*\/([^"/]+)".*/\1/p' "$VKB_LAYER"/share/vulkan/implicit_layer.d/*.json | sed -n 1p)
if [ -z "$LAYER_LIBRARY" ]; then
    echo "no library_path in the layer manifest under $VKB_LAYER/share/vulkan/implicit_layer.d" >&2
    exit 2
fi
if [ -z "${VKB_HOST_TOOLS:-}" ] && [ ! -d /run/opengl-driver ]; then
    echo "this host has no /run/opengl-driver, and the live command's nix-built programs load GPU drivers only from there, so no GPU here can be opened; to test with this host's own vkcube, weston, Xwayland and Vulkan layers, build the layer with meson and run scripts/live-host.sh <build folder> (TESTING.md, Other distributions)" >&2
    exit 2
fi


ICD_DIRS=(/run/opengl-driver/share/vulkan/icd.d "${XDG_CONFIG_HOME:-$HOME/.config}/vulkan/icd.d" /etc/xdg/vulkan/icd.d /etc/vulkan/icd.d
    "${XDG_DATA_HOME:-$HOME/.local/share}/vulkan/icd.d" /usr/local/share/vulkan/icd.d /usr/share/vulkan/icd.d)
keep_logs=0
parallel=0
gallery=""
lsfg_manifest=""
lsfg_dll=""
extra_layers=()
while [ "$#" -gt 0 ]; do
    case "$1" in
        --keep-logs) keep_logs=1 ;;
        --parallel) parallel=1 ;;
        --gallery)
            if [ "$#" -lt 2 ] || [ -z "$2" ]; then
                echo "--gallery needs the folder that keeps the overlay's frames" >&2
                exit 2
            fi
            gallery=$(realpath -m -- "$2")
            shift
            ;;
        --lsfg-vk)
            if [ "$#" -lt 2 ] || [ ! -f "$2" ]; then
                echo "--lsfg-vk needs the layer manifest of an installed lsfg-vk, VkLayer_LSFGVK_frame_generation.json, to read the files the overlay writes" >&2
                exit 2
            fi
            lsfg_manifest=$(realpath -- "$2")
            shift
            ;;
        --lsfg-dll)
            if [ "$#" -lt 2 ] || [ ! -f "$2" ]; then
                echo "--lsfg-dll needs Lossless Scaling's lsfg-vk.dll, which lsfg-vk generates frames with" >&2
                exit 2
            fi
            lsfg_dll=$(realpath -- "$2")
            shift
            ;;
        --layer)
            if [ "$#" -lt 2 ] || [ ! -f "$2" ]; then
                echo "--layer needs a Vulkan layer manifest, a .json file, for the layer lane to run beside vkDaedalus in every order" >&2
                exit 2
            fi
            extra_layers+=("$(realpath -- "$2")")
            shift
            ;;
        --*)
            echo "unknown option $1: the options are --keep-logs, --parallel, --gallery <folder>, --lsfg-vk <manifest>, --lsfg-dll <dll> and --layer <manifest>, then any driver manifests" >&2
            exit 2
            ;;
        *) break ;;
    esac
    shift
done
GALLERY_PARTIAL=""
GALLERY_LAST=""
GALLERY_SEEN=0
GALLERY_STATES=(ui-00-launch-notice ui-01-effects ui-13-fixed-file ui-04a-move-up-greyed ui-02-profile-menu ui-02b-profile-switched ui-02c-safe-anticheat ui-02d-refused-effect
    ui-03-definition ui-04-row-menu ui-04b-effect-removed ui-05-add-effects ui-05b-effects-added ui-06a-name-refused
    ui-06-new-profile ui-06b-profile-made ui-07-delete-profile ui-07b-profile-deleted ui-08-shader-test
    ui-08a-failed-list ui-08b-blocked-list ui-08c-allowed-list ui-08d-no-technique ui-08e-depth-greyed
    ui-09-settings ui-10-performance ui-11-gpus keys-02-listening ui-12-compact-1024x768 set-01-gpu-timing
    set-02-effect-limit set-03-size-menu set-04-auto-apply keys-01-card keys-03-rebound keys-04-key-taken
    dbg-01-window dbg-06-frame-path dbg-07-paths set-05-hud-menu set-06-colours-menu set-07-steel dbg-02-effects-tab dbg-03-log-filters dbg-04-log-search dbg-05-log-cleared sh-01-folder-browser
    sh-02-rescanned sh-03-folders-open sh-04-folders-removed sh-05-pack-removed fx-21-definitions
    fx-22-definition-reset fx-23-definition-refused fx-24-definition-added fx-25-definition-removed sh-11-cache-cleared
    fx-01-rows-moved fx-02-effects-off fx-03-parameter-set fx-04-reset fx-05-half-precision
    fx-06-every-parameter-type fx-07-insert-here fx-08-inserted fx-09-two-chosen fx-10-no-match fx-11-add-cancelled
    fx-12-limit-greyed fx-13-dragged fx-14-compiling fx-15-compile-cancelled fx-16-none-profile fx-17-new-cancelled
    fx-18-delete-cancelled fx-19-rename-profile fx-20-renamed gpus-03-listed gpus-03-moved-down gpus-04-moved-up
    gpus-05-removed ac-01-locked fg-02-dll-missing fg-03-on fg-04-values fg-05-colour-space-menu fg-06-next-launch
    fg-01-frame-generation fg-07-off fg-08-start-only gpus-01-gpu-menu gpus-02-gpus perf-01-timed-offload gpus-06-timed
    size-1080p-performance size-1080p-gpus size-1080p-frame-generation size-1440p-performance size-1440p-gpus size-1440p-frame-generation
    size-1080p-performance-timed size-1080p-gpus-timed size-1440p-performance-timed size-1440p-gpus-timed
    size-720p-performance-three size-720p-gpus-three size-1080p-performance-three size-1080p-gpus-three size-1440p-performance-three size-1440p-gpus-three)
if [ -n "$gallery" ]; then
    for tool in xwd xwdtopnm pamcut pnmtopng; do
        if ! command -v "$tool" >/dev/null; then
            echo "--gallery needs $tool, which is not on PATH" >&2
            exit 2
        fi
    done
    GALLERY_PARTIAL="$gallery.partial"
    rm -rf "$GALLERY_PARTIAL"
    mkdir -p "$GALLERY_PARTIAL"
fi
icds=()
explicit_icds=0
if [ "$#" -gt 0 ]; then
    icds=("$@")
    explicit_icds=1
else
    for dir in "${ICD_DIRS[@]}"; do
        if [ -d "$dir" ]; then
            while IFS= read -r -d '' f; do icds+=("$f"); done < <(find "$dir" -maxdepth 1 -name '*.json' -print0 | sort -z)
        fi
    done
fi
if [ "${#icds[@]}" -eq 0 ]; then
    echo "no Vulkan driver manifest found in ${ICD_DIRS[*]}" >&2
    exit 2
fi

WORK=$(mktemp -d "${TMPDIR:-/tmp}/vkdaedalus-live.XXXXXX")
mkdir -p "${XDG_CACHE_HOME:-$HOME/.cache}"
RUNNABLE=$(mktemp -d "${XDG_CACHE_HOME:-$HOME/.cache}/vkdaedalus-live-runnable.XXXXXX")
keep_work=$keep_logs
KEEP_FLAG="$WORK/keep"
compositor_pids=()
lane_pids=()
process_alive() {
    local state
    state=$(ps -o stat= -p "$1" 2>/dev/null) || return 1
    [[ "$state" != Z* ]]
}

stop_process() {
    local pid="$1" tries=0
    stopped_rc=0
    if ! kill "$pid" 2>/dev/null && process_alive "$pid"; then echo "could not stop the private compositor process $pid" >&2; fi
    while process_alive "$pid" && [ "$tries" -lt 100 ]; do
        sleep 0.1
        tries=$((tries + 1))
    done
    if process_alive "$pid"; then
        echo "the private compositor process $pid ignored TERM for 10 s, so it was killed; its log is in $WORK" >&2
        if ! kill -KILL "$pid" 2>/dev/null && process_alive "$pid"; then echo "could not kill the private compositor process $pid" >&2; fi
        : >"$KEEP_FLAG"
    fi
    wait "$pid" || stopped_rc=$?
}

stop_session() {
    local i pid
    for ((i = ${#compositor_pids[@]} - 1; i >= 0; i--)); do
        pid=${compositor_pids[i]}
        stop_process "$pid"
        if [ "$stopped_rc" -ne 0 ] && [ "$stopped_rc" -ne 143 ]; then
            echo "a private compositor process ($pid) exited with $stopped_rc; its log is in $WORK" >&2
            : >"$KEEP_FLAG"
        fi
    done
    compositor_pids=()
}
report_early_stop() {
    local what="$1" status="$2" command="$3"
    if [ "$status" -ne 0 ] && [[ "$command" != exit* ]] && [[ "$command" != return* ]]; then
        echo "$what stopped at a failing command: \"$command\" (status $status)" >&2
    fi
}
background() {
    (
        trap 'report_early_stop '"$1"' "$?" "$BASH_COMMAND"' EXIT
        "$@"
    ) &
}
driver_finished() {
    local status=0
    wait "$driver" || status=$?
    if [ "$status" -ne 0 ]; then record FAIL "$1 ran to its end" "it ended with status $status before its last step"; fi
}
cleanup() {
    local verdict=$? stopped_at=$BASH_COMMAND pid
    trap '' TERM INT
    report_early_stop "the run" "$verdict" "$stopped_at"
    for pid in "${lane_pids[@]}"; do
        if ! kill "$pid" 2>/dev/null && [ -e "/proc/$pid" ]; then echo "could not stop the lane run $pid" >&2; fi
        wait "$pid" || true
    done
    stop_session
    rm -rf "${RUNNABLE:?}"
    if ! chmod -R u+rwx "$WORK"; then echo "could not make every folder in $WORK writable again" >&2; fi
    if [ "$keep_work" -eq 0 ] && [ ! -e "$KEEP_FLAG" ]; then
        if ! rm -rf "$WORK"; then echo "could not remove $WORK" >&2; fi
    else
        echo "logs kept in $WORK"
    fi
    exit "$verdict"
}
trap cleanup EXIT
trap 'exit 143' TERM
trap 'exit 130' INT

wait_for_socket() {
    local tries=0
    until [ -S "$1" ] || [ "$tries" -ge 100 ]; do
        sleep 0.1
        tries=$((tries + 1))
    done
    [ -S "$1" ]
}

PRIVATE_RUNTIME="$WORK/runtime"
PRIVATE_WAYLAND=vkdaedalus-live
PRIVATE_DISPLAY=
mkdir -m 700 "$PRIVATE_RUNTIME"
printf '[core]\nidle-time=0\n' >"$WORK/weston.ini"

start_session() {
    local logs="$1" icd="$2" n tries=0
    session_problem=""
    env -u DISPLAY -u WAYLAND_DISPLAY XDG_RUNTIME_DIR="$PRIVATE_RUNTIME" VK_DRIVER_FILES="$icd" "${DEVICE_SELECT[@]}" \
        VK_LOADER_LAYERS_DISABLE='~implicit~' weston --backend=headless --renderer=vulkan \
        --width=1296 --height=784 --socket="$PRIVATE_WAYLAND" --config="$WORK/weston.ini" >"$logs/weston.log" 2>&1 &
    compositor_pids+=("$!")
    if ! wait_for_socket "$PRIVATE_RUNTIME/$PRIVATE_WAYLAND"; then
        session_problem="weston did not come up: $(tail -3 "$logs/weston.log" | paste -sd ' ' -)"
        return 1
    fi
    start_x_server "$logs" || return 1
    echo "private headless session: weston on $(sed -n -E "s/.*Vulkan device: '(.*)'/\1/p" "$logs/weston.log" | sed -n 1p), X11 on $PRIVATE_DISPLAY"
}

start_x_server() {
    local logs="$1" n tries=0
    : >"$logs/xwayland.display"
    env -u DISPLAY XDG_RUNTIME_DIR="$PRIVATE_RUNTIME" WAYLAND_DISPLAY="$PRIVATE_WAYLAND" \
        Xwayland -displayfd 3 -geometry 1296x784 -nolisten tcp 3>"$logs/xwayland.display" >"$logs/xwayland.log" 2>&1 &
    x_server_pid=$!
    compositor_pids+=("$x_server_pid")
    until grep -qE '^[0-9]+$' "$logs/xwayland.display" || [ "$tries" -ge 100 ]; do
        sleep 0.1
        tries=$((tries + 1))
    done
    n=$(sed -n 1p "$logs/xwayland.display")
    if [ -z "$n" ] || ! wait_for_socket "/tmp/.X11-unix/X$n"; then
        session_problem="Xwayland did not come up: $(tail -3 "$logs/xwayland.log" | paste -sd ' ' -)"
        return 1
    fi
    PRIVATE_DISPLAY=":$n"
}

x_server_answers() {
    DISPLAY="$PRIVATE_DISPLAY" timeout 5 xdotool getdisplaygeometry >/dev/null 2>&1
}

fresh_x_server() {
    local logs="$1" pid
    local -a kept=()
    if ! x_server_answers; then
        record FAIL "the private X server answers" "X11 on $PRIVATE_DISPLAY stopped answering before this case; Xwayland said: $(grep -m1 -E 'MESA: error|Fatal|error' "$logs/xwayland.log" || tail -1 "$logs/xwayland.log")"
    fi
    stop_process "$x_server_pid"
    for pid in "${compositor_pids[@]}"; do
        if [ "$pid" != "$x_server_pid" ]; then kept+=("$pid"); fi
    done
    compositor_pids=("${kept[@]}")
    if ! start_x_server "$logs"; then
        record FAIL "the private X server restarts" "$session_problem"
        return 1
    fi
    if [ "$(count 'Failed to initialize glamor' "$logs/xwayland.log")" -ne 0 ]; then
        record FAIL "the private X server restarts" "Xwayland fell back to software ($(sed -n 1p "$logs/xwayland.log"))"
        return 1
    fi
}

count() {
    PATTERN="$1" awk '$0 ~ ENVIRON["PATTERN"] { c++ } END { print c + 0 }' "$2"
}

first_match() {
    sed -n -E "s/$1/\\1/p" <<<"$2" | sed -n 1p
}

report_ids() {
    if [ -f "$1" ]; then
        sed -n -E 's/^Validation (Error|Warning): \[ ([^ ]+) \].*/\2/p' "$1" | sort -u
    fi
}

fresh_home() {
    local home="$1" fx="$1-fx"
    rm -rf "${home:?}" "${fx:?}"
    mkdir -p "$home/.config/vkdaedalus/reshade/Textures" "$fx"
    cp "$VKB_FIXTURES/language/macro_spacing.fx" "$VKB_FIXTURES/lut/cube_lut.fx" "$VKB_FIXTURES/effects/compute_smoke.fx" \
        "$VKB_FIXTURES/effects/uniform_smoke.fx" \
        "$VKB_FIXTURES/textures/texture_smoke.fx" "$fx/"
    cp "$VKB_FIXTURES/lut/identity.cube" "$VKB_FIXTURES/textures/solid.png" "$VKB_FIXTURES/textures/solid.dds" \
        "$home/.config/vkdaedalus/reshade/Textures/"
    cat >"$home/.config/vkdaedalus/vkdaedalus.conf" <<EOF
effects = ${2:-cas:smaa:lut:macro_spacing:cube_lut:compute_smoke:texture_smoke:uniform_smoke}
macro_spacing = $fx/macro_spacing.fx
cube_lut = $fx/cube_lut.fx
compute_smoke = $fx/compute_smoke.fx
texture_smoke = $fx/texture_smoke.fx
uniform_smoke = $fx/uniform_smoke.fx
reshadeIncludePath = $fx
enableOnLaunch = True
lutFile = $home/.config/vkdaedalus/reshade/Textures/identity.cube
EOF
}

run() {
    local log="$1" home="$2" icd="$3" layers="$4" enable="$5" validate="$6"
    shift 7
    mkdir -p "$home-no-system-layers"
    local vars=(
        HOME="$home" XDG_CONFIG_HOME="$home/.config" XDG_CACHE_HOME="$home/.cache" XDG_DATA_HOME="$home/.local/share"
        XDG_CONFIG_DIRS="$home-no-system-layers" XDG_DATA_DIRS="$VKB_LAYER/share:$VKB_MOCK/share"
        XDG_RUNTIME_DIR="$PRIVATE_RUNTIME" WAYLAND_DISPLAY="$PRIVATE_WAYLAND" DISPLAY="$PRIVATE_DISPLAY"
        VK_DRIVER_FILES="$icd" VK_LOADER_DEBUG=layer VKDAEDALUS_LOG_LEVEL="$LOG_LEVEL" LD_LIBRARY_PATH="$VKB_WSI_LIBRARIES"
        VKDAEDALUS_LAUNCH_NOTICE=never
    )
    vars+=("${DEVICE_SELECT[@]}")
    if [ -n "$layers" ]; then vars+=(VK_INSTANCE_LAYERS="$layers"); fi
    if [[ "$layers" == *"$MOCK"* ]]; then vars+=(MOCK_FRAMEGEN_REENTER_LOADER=1); else vars+=(DISABLE_MOCK_FRAMEGEN=1); fi
    if [ "$enable" = 1 ]; then vars+=(ENABLE_VKDAEDALUS=1); fi
    local layer_path=""
    if [ "$validate" = 1 ]; then layer_path="$VKB_VALIDATION/share/vulkan/explicit_layer.d:$VKB_FAULT/share/vulkan/explicit_layer.d:$VKB_SCREENSHOT/share/vulkan/explicit_layer.d"; fi
    if [[ "$layers" == *"$LSFG"* ]]; then
        layer_path="${layer_path:+$layer_path:}$LSFG_DIR"
        vars+=(LSFGVK_CONFIG="$LSFG_DIR/conf.toml")
    fi
    if [ -n "${EXTRA_LAYER_PATH:-}" ]; then layer_path="${layer_path:+$layer_path:}$EXTRA_LAYER_PATH"; fi
    if [ -n "$layer_path" ]; then vars+=(VK_ADD_LAYER_PATH="$layer_path"); fi
    if [ "$validate" = 1 ]; then
        vars+=(
            VK_KHRONOS_VALIDATION_DEBUG_ACTION=VK_DBG_LAYER_ACTION_LOG_MSG
            "VK_KHRONOS_VALIDATION_REPORT_FLAGS=error,warn,info"
            VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true
        )
    fi
    vars+=(PATH="$PATH")
    local named=("${vars[@]%%=*}") word
    if [ "${1:-}" = env ]; then
        for word in "${@:2}"; do
            if [[ "$word" == *=* ]]; then named+=("${word%%=*}"); else break; fi
        done
    fi
    printf '%s\n' "${named[@]}" | sort -u >"$log.env"
    if [[ " $* " == *" --wsi xcb "* || " $* " == *" --wsi xlib "* ]] && ! x_server_answers; then
        echo "the private X server $PRIVATE_DISPLAY did not answer, so the game was not started" >"$log"
        echo 125 >"$log.rc"
        return 0
    fi
    local rc=0
    timeout -s KILL "$HANG_SECONDS" env -i "${vars[@]}" "$@" >"$log.stdout" 2>"$log" || rc=$?
    echo "$rc" >"$log.rc"
}

results=()
gpu_failed=0
record() {
    results+=("$(printf '  %-5s %-36s %s' "$1" "$2" "$3")")
    if [ "$1" = FAIL ]; then gpu_failed=1; fi
}

exited_cleanly() {
    local rc
    rc=$(cat "$1.rc")
    case "$rc" in
        0) return 0 ;;
        137) echo "hung: killed after ${HANG_SECONDS}s" ;;
        *) echo "exit $rc: $(grep -v -E '^\[Vulkan Loader\]|^vkDaedalus (trace|debug|info)' "$1" | tail -1)" ;;
    esac
    return 1
}

validation_verdict() {
    local log="$1" control="$2" below new
    if [ ! -f "$log.stdout" ] || [ "$(count 'CURRENT-VALIDATION-ENABLED' "$log.stdout")" -lt 1 ]; then
        echo "validation never reported itself active"
        return 1
    fi
    below=$(awk -v v="\"$VALIDATION\"" -v b="\"$BASALT\"" '
        /Insert instance layer/ { if (index($0, v) && !vi) vi = NR; if (index($0, b) && !bi) bi = NR }
        END { print (vi && bi && vi < bi) ? "yes" : "no" }' "$log")
    if [ "$below" != yes ]; then
        echo "validation is not under the layer in the loader chain"
        return 1
    fi
    new=$(comm -23 <(report_ids "$log.stdout") <(report_ids "$control.stdout") | paste -sd ' ' -)
    if [ -n "$new" ]; then
        echo "validation errors or warnings the layer introduced: $new"
        return 1
    fi
}

native_wsi=wayland

vkcube_of_run() {
    local home="$1" pid environ
    for pid in $(pgrep -x vkcube || true); do
        environ=$(tr '\0' '\n' <"/proc/$pid/environ" 2>/dev/null) || continue
        if grep -qx "HOME=$home" <<<"$environ"; then
            echo "$pid"
            return 0
        fi
    done
    return 1
}

caught_signals() {
    local log="$1" out="$2" need="$3" home="$4" pid i
    for ((i = 0; i < 600; i++)); do
        if pid=$(vkcube_of_run "$home"); then
            if [ "$need" -eq 0 ]; then
                sleep 2
            elif [ "$(count 'present cycle' "$log")" -lt "$need" ]; then
                sleep 0.1
                continue
            fi
            awk '/^SigCgt:/ { print $2 }' "/proc/$pid/status" >"$out"
            return 0
        fi
        sleep 0.1
    done
    return 1
}

lane_e2e() {
    local icd="$1" dir="$2" why name log pattern stray watcher control_mask cold_mask taken warmed
    local home="$dir/e2e-home" control="$dir/e2e-control.log"
    fresh_home "$home"
    background caught_signals "$control" "$dir/control.sigcgt" 0 "$dir/e2e-control-home"
    watcher=$!
    run "$control" "$dir/e2e-control-home" "$icd" "$VALIDATION" 0 1 -- vkcube --c "$FRAMES" --wsi "$native_wsi"
    if ! why=$(exited_cleanly "$control"); then
        record FAIL "control run without the layer" "$why"
        return
    fi
    if ! wait "$watcher"; then
        record FAIL "control run without the layer" "could not read its caught signals"
        return
    fi
    background caught_signals "$dir/e2e-cold.log" "$dir/cold.sigcgt" 10 "$home"
    watcher=$!
    run "$dir/e2e-cold.log" "$home" "$icd" "$BASALT:$VALIDATION" 1 1 -- vkcube --c "$FRAMES" --wsi "$native_wsi"
    if wait "$watcher"; then
        control_mask=$(cat "$dir/control.sigcgt")
        cold_mask=$(cat "$dir/cold.sigcgt")
        taken=$(((16#$cold_mask & 0xa0) & ~(16#$control_mask & 0xa0)))
        if [ "$taken" -eq 0 ]; then
            record PASS "game keeps its signal handlers" "the layer catches neither SIGABRT nor SIGFPE (SigCgt $cold_mask)"
        else
            record FAIL "game keeps its signal handlers" "the layer took over $([ $((taken & 0x20)) -ne 0 ] && echo SIGABRT) $([ $((taken & 0x80)) -ne 0 ] && echo SIGFPE) (SigCgt $control_mask without the layer, $cold_mask with it)"
        fi
    else
        record FAIL "game keeps its signal handlers" "could not read the app's caught signals while the layer ran"
    fi
    run "$dir/e2e-warm.log" "$home" "$icd" "$BASALT:$VALIDATION" 1 1 -- vkcube --c "$FRAMES" --wsi "$native_wsi"
    for name in cold warm; do
        log="$dir/e2e-$name.log"
        if ! why=$(exited_cleanly "$log"); then
            record FAIL "e2e $name: run" "$why"
            continue
        fi
        if ! why=$(validation_verdict "$log" "$control"); then
            record FAIL "e2e $name: validation" "$why"
            continue
        fi
        if [ "$(count 'vkDaedalus err:|Vulkan Loader.*ERROR' "$log")" -ne 0 ]; then
            record FAIL "e2e $name: errors" "$(grep -m1 -E 'vkDaedalus err:|Vulkan Loader.*ERROR' "$log")"
            continue
        fi
        local missing=()
        for pattern in 'creating effect 0: cas' 'creating effect [0-9]+: smaa' 'creating effect [0-9]+: lut' 'creating effect [0-9]+: macro_spacing' 'creating effect [0-9]+: cube_lut' 'creating effect [0-9]+: compute_smoke' \
            'creating effect [0-9]+: texture_smoke' 'creating effect [0-9]+: uniform_smoke' 'effect uniform_smoke: [0-9]+ uniform buffers of [0-9]+ bytes' 'created fake swapchain images' 'present cycle' \
            'texture solid\.png: 4x4 from .*, uploaded at 16x16 with 4 channel' \
            'texture solid\.png: 4x4 from .*, uploaded at 8x8 with 2 channel' \
            'texture solid\.dds: 4x4 from .*, uploaded at 4x4 with 4 channel'; do
            if [ "$(count "$pattern" "$log")" -lt 1 ]; then missing+=("$pattern"); fi
        done
        if [ "$name" = cold ]; then pattern='created reshade shaderModule'; else pattern='compiled effects: reused'; fi
        if [ "$(count "$pattern" "$log")" -lt 1 ]; then missing+=("$pattern"); fi
        if [ "${#missing[@]}" -gt 0 ]; then
            record FAIL "e2e $name: features" "missing: ${missing[*]}"
            continue
        fi
        if [ "$(count 'overlay recorded a frame' "$log")" -ne 0 ]; then
            record FAIL "e2e $name: hidden overlay" "the overlay was never opened yet recorded $(count 'overlay recorded a frame' "$log") frames"
            continue
        fi
        if [ "$(count 'per-effect GPU timing enabled' "$log")" -ne 0 ]; then
            record FAIL "e2e $name: GPU timing" "per-effect GPU timing came on under default settings"
            continue
        fi
        warmed=$(sed -n 's/.*compile cache warm-up: \([0-9]*\) effects$/\1/p' "$log" | sed -n 1p)
        if [ "$warmed" != "$(find "$home-fx" -name '*.fx' | wc -l)" ]; then
            record FAIL "e2e $name: compile warm-up" "the first warm-up gathered ${warmed:-no} effects of the $(find "$home-fx" -name '*.fx' | wc -l) ReShade effects in the chain, so they compiled under the layer lock instead"
            continue
        fi
        record PASS "e2e $name ($native_wsi)" "$(count 'present cycle' "$log") presents, eight effects: CAS, SMAA, LUT and five ReShade incl. compute, PNG, RG8 and DDS textures and per-frame uniforms, no layer errors"
    done
    stray=$(find "$home" -mindepth 1 -maxdepth 1 ! -name .config ! -name .cache ! -name .local | paste -sd ' ' -)
    if [ -n "$stray" ]; then
        record FAIL "files stay in XDG directories" "$stray"
    else
        record PASS "files stay in XDG directories" "home holds only .config, .cache, .local"
    fi
}

lsfg_frames_ready() {
    local dir="$1"
    if [ -z "$lsfg_manifest" ] || [ -z "$lsfg_dll" ]; then return 1; fi
    rm -rf "${dir:?}"
    mkdir -p "$dir"
    cp "$lsfg_manifest" "$dir/"
    printf '%s\n' 'version = 2' '' '[global]' "dll = \"$lsfg_dll\"" 'log_level = "debug"' '' \
        '[[profile]]' 'name = "live vkcube"' 'active_in = "vkcube"' 'multiplier = 2' 'flow_scale = 1.0' 'performance_mode = true' '' \
        '[[profile]]' 'name = "live gpu-choice-probe"' 'active_in = "gpu-choice-probe"' 'multiplier = 2' 'flow_scale = 1.0' 'performance_mode = true' \
        >"$dir/conf.toml"
}

lsfg_frames_skip() {
    echo "lsfg-vk generates frames only with both named: --lsfg-vk <its layer manifest> --lsfg-dll <Lossless Scaling's lsfg-vk.dll>"
}

lsfg_ran() {
    local log="$1" why line
    if ! why=$(exited_cleanly "$log"); then
        echo "$why"
        return 1
    fi
    for line in 'Hooked new Vulkan device creation' 'Hooked new Vulkan swapchain creation' 'Initializing lsfg-vk instance'; do
        if [ "$(count "^\\(lsfg-vk\\) \\[(INFO|DEBUG)\\]: $line" "$log")" -lt 1 ]; then
            echo "lsfg-vk never logged: $line"
            return 1
        fi
    done
    if [ "$(count '^\(lsfg-vk\) \[(WARN|ERROR)\]' "$log")" -ne 0 ]; then
        echo "lsfg-vk said: $(grep -m1 -E '^\(lsfg-vk\) \[(WARN|ERROR)\]' "$log")"
        return 1
    fi
}

lsfg_frames_verdict() {
    local log="$1" control="$2" frames="$3" want="$4" why presents ratio
    if ! why=$(lsfg_ran "$log"); then
        echo "$why"
        return 1
    fi
    if [ "$(count 'vkDaedalus err:' "$log")" -ne 0 ]; then
        grep -m1 'vkDaedalus err:' "$log"
        return 1
    fi
    if [ "$(count 'vkDaedalus info: +the layer stays out of the Vulkan instance of lsfg-vk: it enables no surface' "$log")" -lt 1 ]; then
        echo "the layer hooked the Vulkan instance lsfg-vk makes for its own frame generation"
        return 1
    fi
    if ! why=$(validation_verdict "$log" "$control"); then
        echo "validation: $why"
        return 1
    fi
    presents=$(count 'present cycle' "$log")
    ratio=$(awk -v p="$presents" -v f="$frames" 'BEGIN { printf "%.0f", p / f }')
    if [ "$ratio" != "$want" ]; then
        echo "the layer presented $presents times for $frames game frames, want $want each"
        return 1
    fi
    echo "$presents"
}

lsfg_live_mark() {
    printf '%s %s %s\n' "$2" "$(count 'present cycle' "$1")" "$(count '^fault_inject: present [0-9]+$' "$1")" >>"$1.marks"
}

drive_lsfg_live() {
    local log="$1" conf="$2"
    : >"$log.marks"
    if ! wait_for_line 'present cycle' 60 "$log"; then
        echo "never presented" >>"$log.marks"
        return 0
    fi
    sleep 2
    lsfg_live_mark "$log" base-a
    sleep 3
    lsfg_live_mark "$log" base-b
    hold_key_until Insert 'frame generation key: off' 1 "$log" || echo "no off" >>"$log.marks"
    sleep 1
    lsfg_live_mark "$log" off-a
    sleep 3
    lsfg_live_mark "$log" off-b
    hold_key_until Insert 'frame generation key: [0-9]+x, on' 1 "$log" || echo "no on" >>"$log.marks"
    sleep 1
    lsfg_live_mark "$log" on-a
    sleep 3
    lsfg_live_mark "$log" on-b
    sed -i 's/^multiplier = 2$/multiplier = 3/' "$conf"
    wait_for_count 'Configuration file has changed, reloading' 3 "$log" || echo "lsfg-vk never reloaded its file a third time" >>"$log.marks"
    sleep 1
    lsfg_live_mark "$log" three-a
    sleep 3
    lsfg_live_mark "$log" three-b
    sed -i 's/^multiplier = 3$/multiplier = 1/' "$conf"
    wait_for_count 'Configuration file has changed, reloading' 4 "$log" || echo "lsfg-vk never reloaded its file a fourth time" >>"$log.marks"
    sleep 1
    lsfg_live_mark "$log" file-off-a
    sleep 3
    lsfg_live_mark "$log" file-off-b
}

lsfg_live_ratio() {
    awk -v from="$2-a" -v to="$2-b" -v game="$3" '
        $1 == from { v0 = $2; f0 = $3 } $1 == to { v1 = $2; f1 = $3 }
        END {
            dv = v1 - v0; df = f1 - f0
            if (dv <= 0 || df <= 0) { print "none"; exit }
            printf "%.2f", game == "layer" ? df / dv : dv / df
        }' "$1.marks"
}

lsfg_present_gaps() {
    awk '/^fault_inject: present [0-9]+$/ { if (last) print ($3 - last) / 1e6; last = $3 }' "$1" | sort -n |
        awk '{ v[NR] = $1 } END {
            if (NR == 0) { print "none"; exit }
            printf "median %.3f ms, shortest 1%% %.3f ms, shortest %.3f ms, so a counter reading one gap would show up to %.0f FPS", v[int((NR + 1) / 2)], v[int(NR / 100) + 1], v[1], (v[1] > 0 ? 1000 / v[1] : 0)
        }'
}

lsfg_live_case() {
    local icd="$1" dir="$2" order="$3" layers game log home driver phase want got wrong=() name side
    name="lsfg-vk switched live, effects $order"
    if [ "$order" = before ]; then
        layers="$BASALT:$LSFG:$FAULT" game=layer side=below
    else
        layers="$FAULT:$LSFG:$BASALT" game=fault side=above
    fi
    log="$dir/lsfg-live-$order.log"
    home="$dir/lsfg-live-$order-home"
    fresh_home "$home" cas:smaa:uniform_smoke
    printf 'overlayKey = %s\n' "$OVERLAY_KEY" >"$home/.config/vkdaedalus/settings.conf"
    lsfg_frames_ready "$LSFG_DIR"
    background drive_lsfg_live "$log" "$LSFG_DIR/conf.toml"
    driver=$!
    run "$log" "$home" "$icd" "$layers" 1 1 -- env FAULT_INJECT_COUNT_PRESENTS=each vkcube --c 2000 --wsi xcb
    driver_finished "drive_lsfg_live"
    if ! why=$(exited_cleanly "$log"); then
        record FAIL "$name" "$why"
        return
    fi
    if [ "$(count 'frame generation key: .*from the next launch' "$log")" -ne 0 ]; then
        wrong+=("the key said $(grep -m1 -o 'frame generation key: .*' "$log")")
    fi
    if [ "$(count 'vkDaedalus err:' "$log")" -ne 0 ]; then
        wrong+=("$(grep -m1 'vkDaedalus err:' "$log")")
    fi
    record MEAS "$name: gaps between presents $side lsfg-vk" "$(lsfg_present_gaps "$log")"
    for phase in base:2 off:1 on:2 three:3 file-off:1; do
        want=${phase#*:}
        got=$(lsfg_live_ratio "$log" "${phase%%:*}" "$game")
        record MEAS "$name: ${phase%%:*}" "$got frames shown for every frame the game drew, want $want"
        if [ "$got" = none ] || ! awk -v g="$got" -v w="$want" 'BEGIN { exit !(g >= w * 0.85 && g <= w * 1.15) }'; then
            wrong+=("${phase%%:*} $got, want $want")
        fi
    done
    if [ "${#wrong[@]}" -gt 0 ]; then
        record FAIL "$name" "shown per drawn frame: $(printf '%s; ' "${wrong[@]}")marks: $(tr '\n' ' ' <"$log.marks")"
    else
        record PASS "$name" "while vkcube ran with cas, smaa and uniform_smoke, the key turned frame generation off and on, then lsfg-vk's own file went to 3x and to 1x: 2, 1, 2, 3 and 1 frames shown for each drawn, counted on both sides of lsfg-vk"
    fi
}

lane_framegen() {
    local icd="$1" dir="$2" why order layers want log presents frames ratio anomalies hooked nested
    local control="$dir/framegen-control.log"
    run "$control" "$dir/framegen-control-home" "$icd" "$MOCK:$VALIDATION" 0 1 -- vkcube --c "$FRAMES" --wsi "$native_wsi"
    if ! why=$(exited_cleanly "$control"); then
        record FAIL "framegen control (mock alone)" "$why"
        return
    fi
    for order in above below; do
        if [ "$order" = above ]; then
            layers="$BASALT:$MOCK:$VALIDATION" want=1
        else
            layers="$MOCK:$BASALT:$VALIDATION" want=2
        fi
        log="$dir/framegen-$order.log"
        fresh_home "$dir/framegen-$order-home"
        run "$log" "$dir/framegen-$order-home" "$icd" "$layers" 1 1 -- vkcube --c "$FRAMES" --wsi "$native_wsi"
        if ! why=$(exited_cleanly "$log"); then
            record FAIL "layer $order framegen: run" "$why"
            continue
        fi
        if ! why=$(validation_verdict "$log" "$control"); then
            record FAIL "layer $order framegen: validation" "$why"
            continue
        fi
        presents=$(count 'present cycle' "$log")
        frames=$(count 'mock_framegen: frame [0-9]+ ok' "$log")
        if [ "$frames" -le 20 ]; then
            record FAIL "layer $order framegen" "only $frames generated frames"
            continue
        fi
        ratio=$(awk -v p="$presents" -v f="$frames" 'BEGIN { printf "%.0f", p / f }')
        anomalies=$(count 'mock_framegen: .*(failed|STALL|TIMEOUT)|down-chain present returned|Vulkan Loader.*ERROR|vkDaedalus err:' "$log")
        hooked=$(count 'mock_framegen: device hooked' "$log")
        nested=$(count 'mock_framegen: nested loader re-entry ok' "$log")
        if [ "$ratio" = "$want" ] && [ "$anomalies" -eq 0 ] && [ "$hooked" -ge 1 ] && [ "$nested" -eq 1 ]; then
            record PASS "layer $order framegen" "$presents presents for $frames generated frames, loader re-entry ok"
        else
            record FAIL "layer $order framegen" "ratio=$ratio want=$want anomalies=$anomalies hooked=$hooked nested=$nested"
        fi
    done
    local LSFG_DIR="$dir/real-lsfg-frames" lsfgControl="$dir/lsfg-frames-control.log"
    if ! lsfg_frames_ready "$LSFG_DIR"; then
        record SKIP "lsfg-vk frames with the layer" "$(lsfg_frames_skip)"
        return 0
    fi
    run "$lsfgControl" "$dir/lsfg-frames-control-home" "$icd" "$LSFG:$VALIDATION" 0 1 -- vkcube --c "$FRAMES" --wsi "$native_wsi"
    if ! why=$(lsfg_ran "$lsfgControl"); then
        record FAIL "lsfg-vk frames alone" "$why"
        return 0
    fi
    for order in before after; do
        if [ "$order" = before ]; then
            layers="$BASALT:$LSFG:$VALIDATION" want=1
        else
            layers="$LSFG:$BASALT:$VALIDATION" want=2
        fi
        log="$dir/lsfg-frames-$order.log"
        fresh_home "$dir/lsfg-frames-$order-home" cas
        run "$log" "$dir/lsfg-frames-$order-home" "$icd" "$layers" 1 1 -- vkcube --c "$FRAMES" --wsi "$native_wsi"
        if ! why=$(lsfg_frames_verdict "$log" "$lsfgControl" "$FRAMES" "$want"); then
            record FAIL "lsfg-vk frames, effects $order" "$why"
        elif [ "$order" = before ]; then
            record PASS "lsfg-vk frames, effects $order" "cas on all $why frames the game drew, lsfg-vk below it made its device, swapchain and 2x pipeline with no warning, validation clean against lsfg-vk alone"
        else
            record PASS "lsfg-vk frames, effects $order" "cas on $why frames for the $FRAMES the game drew, so every frame lsfg-vk made at 2x went through the layer, validation clean against lsfg-vk alone"
        fi
    done
    if [ ! -x "$VKB_LAYER/bin/vkdaedalus-run" ]; then
        record SKIP "the order layers put lsfg-vk where the profile asks" "this build has no vkdaedalus-run (a plain meson build installs none)"
        return 0
    fi
    local home chain top
    for order in before after; do
        log="$dir/lsfg-order-layers-$order.log" home="$dir/lsfg-order-layers-$order-home"
        fresh_home "$home" cas
        mkdir -p "$home/.config/vkdaedalus/profiles"
        if [ "$order" = after ]; then printf 'effectsAfterFrameGeneration = true\n' >"$home/.config/vkdaedalus/profiles/default.conf"; fi
        run "$log" "$home" "$icd" "" 1 0 -- env LSFGVK_CONFIG="$LSFG_DIR/conf.toml" VK_ADD_IMPLICIT_LAYER_PATH="$LSFG_DIR" "$VKB_LAYER/bin/vkdaedalus-run" vkcube --c "$FRAMES" --wsi "$native_wsi"
        chain=$(awk '/vkCreateInstance layer callstack setup to:/ { p = 1; next } p && /<Drivers>|<Physical Devices>|callstack setup/ { exit } p' "$log" | grep -oE 'VK_LAYER_[A-Za-z0-9_]+$' | tr '\n' ' ')
        top=$(grep -oE "$BASALT|$LSFG" <<<"$chain" | head -1)
        if ! why=$(lsfg_ran "$log"); then
            record FAIL "the order layers put lsfg-vk where the profile asks, effects $order" "$why"
        elif [ "$order" = before ] && [ "$top" != "$BASALT" ]; then
            record FAIL "the order layers put lsfg-vk where the profile asks, effects $order" "with the profile asking for before, the loader's chain was: $chain"
        elif [ "$order" = after ] && [ "$top" != "$LSFG" ]; then
            record FAIL "the order layers put lsfg-vk where the profile asks, effects $order" "with the profile asking for after, the loader's chain was: $chain"
        else
            record PASS "the order layers put lsfg-vk where the profile asks, effects $order" "started with vkdaedalus-run and both layers implicit, the loader's chain was $chain"
        fi
    done
    for order in before after; do
        lsfg_live_case "$icd" "$dir" "$order"
    done
}

layer_orders() {
    local prefix="$1" i
    shift
    if [ "$#" -eq 0 ]; then
        echo "${prefix#:}"
        return
    fi
    for ((i = 1; i <= $#; i++)); do
        layer_orders "$prefix:${!i}" "${@:1:i-1}" "${@:i+1}"
    done
}

placed_orders() {
    local -a rest=("$@") placed
    local n=$((${#rest[@]} + 2)) b l k used
    for ((b = 0; b < n; b++)); do
        for ((l = 0; l < n; l++)); do
            if [ "$b" -eq "$l" ]; then continue; fi
            placed=() used=0
            for ((k = 0; k < n; k++)); do
                if [ "$k" -eq "$b" ]; then
                    placed+=("$BASALT")
                elif [ "$k" -eq "$l" ]; then
                    placed+=("$LSFG")
                else
                    placed+=("${rest[used]}")
                    used=$((used + 1))
                fi
            done
            (
                IFS=:
                echo "${placed[*]}"
            )
        done
    done
}

copy_layer_manifest() {
    local manifest="$1" dir="$2" library resolved
    library=$(sed -n 's/.*"library_path": *"\([^"]*\)".*/\1/p' "$manifest" | sed -n 1p)
    if [ -z "$library" ]; then return 1; fi
    case "$library" in
        /*) resolved=$library ;;
        */*) resolved=$(cd "$(dirname "$manifest")" && realpath -m -- "$library") ;;
        *) resolved=$library ;;
    esac
    mkdir -p "$dir"
    LIBRARY="$resolved" awk '{ if (!done && $0 ~ /"library_path"/) { sub(/"library_path": *"[^"]*"/, "\"library_path\": \"" ENVIRON["LIBRARY"] "\""); done = 1 } print }' \
        "$manifest" >"$dir/$(basename "$manifest")"
}

layer_order_case() {
    local icd="$1" dir="$2" order="$3" wsi="$4" frames="$5" slug log home why
    local layers presents bottom span place i reported game shown truthGame truthShown lsfgAt basaltAt want wantBottom opened drawn
    slug=$(tr ':' '-' <<<"$order" | tr '[:upper:]' '[:lower:]' | sed 's/vk_layer_//g')
    log="$dir/layers-$wsi-$slug.log"
    home="$dir/layers-$wsi-$slug-home"
    fresh_home "$home" cas:smaa:uniform_smoke
    printf 'overlayKey = %s\n' "$OVERLAY_KEY" >"$home/.config/vkdaedalus/settings.conf"
    layers="$order:$FAULT"
    local presser="" HANG_SECONDS=60
    if [ "$wsi" = xcb ]; then
        fresh_x_server "$dir" || return 1
        background press_overlay_key "$log"
        presser=$!
    fi
    run "$log" "$home" "$icd" "$layers" 1 1 -- env FAULT_INJECT_COUNT_PRESENTS=1 vkcube --c "$frames" --wsi "$wsi"
    if [ -n "$presser" ]; then wait "$presser" || true; fi
    if ! why=$(exited_cleanly "$log"); then
        record FAIL "layers $order ($wsi)" "$why"
        return 1
    fi
    if [ "$(count 'vkDaedalus err:' "$log")" -ne 0 ]; then
        record FAIL "layers $order ($wsi)" "$(grep -m1 'vkDaedalus err:' "$log")"
        return 1
    fi
    presents=$(count 'present cycle' "$log")
    bottom=$(sed -n 's/^fault_inject: counted \([0-9]*\) presents over .*/\1/p' "$log" | tail -1)
    span=$(sed -n 's/^fault_inject: counted [0-9]* presents over \([0-9.]*\) ms$/\1/p' "$log" | tail -1)
    if [ -z "$bottom" ] || [ -z "$span" ]; then
        record FAIL "layers $order ($wsi)" "the counting layer at the bottom reported nothing"
        return 1
    fi
    local -a parts
    IFS=: read -r -a parts <<<"$order"
    lsfgAt="" basaltAt=""
    for i in "${!parts[@]}"; do
        if [ "${parts[i]}" = "$LSFG" ]; then lsfgAt=$i; fi
        if [ "${parts[i]}" = "$BASALT" ]; then basaltAt=$i; fi
    done
    want=$frames wantBottom=$frames place="lsfg-vk is not loaded"
    if [ -n "$lsfgAt" ]; then
        wantBottom=$((frames * 2))
        if [ "$lsfgAt" -lt "$basaltAt" ]; then
            want=$((frames * 2)) place="lsfg-vk runs above the layer"
        else
            place="lsfg-vk runs below the layer"
        fi
    fi
    reported=$(grep -m1 -F "frame rates over the swapchain's life" "$log" | sed -E 's/^vkDaedalus info: +//')
    game=$(sed -n 's/.*: game \([0-9.]*\) FPS.*/\1/p' <<<"$reported")
    shown=$(sed -n 's/.*, shown \([0-9.]*\) FPS.*/\1/p' <<<"$reported")
    truthGame=$(awk -v f="$frames" -v s="$span" 'BEGIN { printf "%.1f", (s > 0 ? f * 1000 / s : 0) }')
    truthShown=$(awk -v b="$bottom" -v s="$span" 'BEGIN { printf "%.1f", (s > 0 ? b * 1000 / s : 0) }')
    record MEAS "layers $order ($wsi)" "bottom $bottom presents in $span ms: game $truthGame FPS, shown $truthShown; the layer saw $presents and reports ${reported:-nothing}"
    local wrong=()
    if ! awk -v p="$presents" -v w="$want" 'BEGIN { exit !(p >= w * 0.9 - 2 && p <= w + 2) }'; then wrong+=("the layer saw $presents presents, want about $want"); fi
    if ! awk -v p="$bottom" -v w="$wantBottom" 'BEGIN { exit !(p >= w * 0.9 - 2 && p <= w + 2) }'; then wrong+=("$bottom presents reached the driver, want about $wantBottom"); fi
    if [ "$place" != "lsfg-vk is not loaded" ] && [ "$(count "vkDaedalus info: +$place" "$log")" -lt 1 ]; then wrong+=("the layer never said $place"); fi
    if [ "$place" = "lsfg-vk is not loaded" ] && [ "$(count 'vkDaedalus info: +lsfg-vk runs' "$log")" -ne 0 ]; then wrong+=("the layer placed an lsfg-vk that is not loaded"); fi
    if [ -z "$game" ] || [ -z "$shown" ]; then
        wrong+=("the layer logged no frame rates when the swapchain went")
    else
        if ! awk -v r="$game" -v t="$truthGame" 'BEGIN { exit !(r >= t * 0.85 && r <= t * 1.15) }'; then wrong+=("it reports the game at $game FPS, truth $truthGame"); fi
        if ! awk -v r="$shown" -v t="$truthShown" 'BEGIN { exit !(r >= t * 0.85 && r <= t * 1.15) }'; then wrong+=("it reports $shown FPS shown, truth $truthShown"); fi
    fi
    local effect
    for effect in cas smaa uniform_smoke; do
        if [ "$(count "creating effect [0-9]+: $effect\$" "$log")" -lt 1 ]; then wrong+=("the layer never made $effect"); fi
    done
    if [ "$wsi" = xcb ]; then
        opened=$(count 'overlay opened' "$log")
        drawn=$(count 'overlay recorded a frame' "$log")
        if [ "$opened" -lt 1 ] || [ "$drawn" -lt 5 ]; then wrong+=("the overlay opened $opened times and drew $drawn frames"); fi
    fi
    if [ "${#wrong[@]}" -gt 0 ]; then
        record FAIL "layers $order ($wsi)" "$(printf '%s; ' "${wrong[@]}")"
        return 1
    fi
}

lane_layers() {
    local icd="$1" dir="$2" manifest name others=() subset mask i order count=0 failed=0 why layer wsi
    local LSFG_DIR="$dir/real-lsfg-layers" frames="$LAYER_FRAMES" only_x11 member whyX11
    local -a xcb_only=()
    local layer_dir="$dir/extra-layers"
    local EXTRA_LAYER_PATH="$layer_dir"
    for manifest in "${extra_layers[@]}"; do
        if ! copy_layer_manifest "$manifest" "$layer_dir"; then
            record FAIL "a layer named with --layer loads" "$manifest names no library_path"
            continue
        fi
        name=$(sed -n 's/.*"name": *"\([^"]*\)".*/\1/p' "$manifest" | sed -n 1p)
        run "$dir/layers-alone-$name.log" "$dir/layers-alone-$name-home" "$icd" "$name:$FAULT" 0 1 -- env FAULT_INJECT_COUNT_PRESENTS=1 vkcube --c "$frames" --wsi "$native_wsi"
        if ! why=$(exited_cleanly "$dir/layers-alone-$name.log"); then
            run "$dir/layers-alone-$name-xcb.log" "$dir/layers-alone-$name-xcb-home" "$icd" "$name:$FAULT" 0 1 -- env FAULT_INJECT_COUNT_PRESENTS=1 vkcube --c "$frames" --wsi xcb
            if whyX11=$(exited_cleanly "$dir/layers-alone-$name-xcb.log"); then
                record SKIP "layers with $name on $native_wsi" "$name alone fails on $native_wsi, so its orders run on X11 only: $why"
                xcb_only+=("$name")
                others+=("$name")
            else
                record SKIP "layers with $name" "$name alone fails here, so its orders are not tried: $why; on X11: $whyX11"
            fi
            continue
        fi
        others+=("$name")
    done
    if lsfg_frames_ready "$LSFG_DIR"; then
        others+=("$LSFG")
    else
        record SKIP "layers with lsfg-vk" "$(lsfg_frames_skip)"
    fi
    if [ "${#others[@]}" -eq 0 ]; then
        record SKIP "every layer order runs" "no other layer named: --layer <manifest> and --lsfg-vk with --lsfg-dll add them"
        return 0
    fi
    local -a rest=()
    for layer in "${others[@]}"; do
        if [ "$layer" != "$LSFG" ]; then rest+=("$layer"); fi
    done
    for ((mask = 1; mask < (1 << ${#others[@]}); mask++)); do
        subset=()
        for ((i = 0; i < ${#others[@]}; i++)); do
            if ((mask & (1 << i))); then subset+=("${others[i]}"); fi
        done
        if [ "${#subset[@]}" -gt 3 ]; then continue; fi
        only_x11=0
        for member in "${subset[@]}"; do
            if [[ " ${xcb_only[*]} " == *" $member "* ]]; then only_x11=1; fi
        done
        while IFS= read -r order; do
            if [ "$only_x11" = 0 ]; then
                count=$((count + 1))
                layer_order_case "$icd" "$dir" "$order" "$native_wsi" "$frames" || failed=$((failed + 1))
            fi
            if [ "${#subset[@]}" -eq 1 ] || [ "$only_x11" = 1 ]; then
                count=$((count + 1))
                layer_order_case "$icd" "$dir" "$order" xcb "$frames" || failed=$((failed + 1))
            fi
        done < <(layer_orders "" "$BASALT" "${subset[@]}")
    done
    if [ "${#others[@]}" -gt 3 ] && [ "${#rest[@]}" -lt "${#others[@]}" ]; then
        local -a wsis=("$native_wsi" xcb)
        if [ "${#xcb_only[@]}" -gt 0 ]; then wsis=(xcb); fi
        while IFS= read -r order; do
            for wsi in "${wsis[@]}"; do
                count=$((count + 1))
                layer_order_case "$icd" "$dir" "$order" "$wsi" "$frames" || failed=$((failed + 1))
            done
        done < <(placed_orders "${rest[@]}")
    fi
    if [ "$failed" -eq 0 ]; then
        record PASS "every layer order runs" "$count runs of vkDaedalus with ${others[*]}: every order of every set of up to three, every place of vkDaedalus and lsfg-vk among all of them, the overlay opened on X11${xcb_only[*]:+, ${xcb_only[*]} on X11 only}; frames, presents, its place and its figures all as counted"
    fi
}

lane_surface() {
    local icd="$1" dir="$2" name="$3" mode="$4" why log control layers enable validate
    shift 5
    log="$dir/surface-$name.log"
    control="$dir/surface-$name-control.log"
    if [ "$mode" = validated ]; then
        layers="$BASALT:$VALIDATION" enable=1 validate=1
        run "$control" "$dir/surface-$name-control-home" "$icd" "$VALIDATION" 0 1 -- "$@"
        if ! why=$(exited_cleanly "$control"); then
            record FAIL "surface $name: control without the layer" "$why"
            return
        fi
    else
        layers="" enable=0 validate=0
    fi
    fresh_home "$dir/surface-$name-home"
    run "$log" "$dir/surface-$name-home" "$icd" "$layers" "$enable" "$validate" -- "$@"
    if ! why=$(exited_cleanly "$log"); then
        record FAIL "surface $name" "$why"
    elif [ "$mode" = validated ] && ! why=$(validation_verdict "$log" "$control"); then
        record FAIL "surface $name: validation" "$why"
    elif [ "$(count 'vkDaedalus err:' "$log")" -ne 0 ]; then
        record FAIL "surface $name" "$(grep -m1 'vkDaedalus err:' "$log")"
    elif [ "$(count 'present cycle' "$log")" -lt 1 ]; then
        record FAIL "surface $name" "the layer never presented"
    else
        record PASS "surface $name" "$(count 'present cycle' "$log") presents"
    fi
    if [[ "$name" == x11-* || "$name" == wayland* ]]; then
        local queries
        queries=$(sed -n 's/.*frame statistics: .*, \([0-9][0-9]*\) mouse queries$/\1/p' "$log" | tail -1)
        if [ -z "$queries" ]; then
            record FAIL "surface $name: the mouse is left alone" "no frame statistics line names the mouse queries"
        elif [ "$queries" -ne 0 ]; then
            record FAIL "surface $name: the mouse is left alone" "the layer read the mouse $queries times with no effect reading it and the overlay closed"
        else
            record PASS "surface $name: the mouse is left alone" "the layer never read the mouse over the run, since no effect reads it and the overlay stayed closed"
        fi
    fi
}

lane_surfaces() {
    local icd="$1" dir="$2"
    lane_surface "$icd" "$dir" wayland validated -- vkcube --c "$FRAMES" --wsi wayland
    lane_surface "$icd" "$dir" x11-xcb validated -- vkcube --c "$FRAMES" --wsi xcb
    lane_surface "$icd" "$dir" x11-xlib validated -- vkcube --c "$FRAMES" --wsi xlib
    if ! command -v gamescope >/dev/null; then
        record SKIP "surface gamescope" "gamescope is not packaged for this platform"
        return
    fi
    local control="$dir/surface-gamescope-control.log" why
    fresh_home "$dir/surface-gamescope-control-home"
    run "$control" "$dir/surface-gamescope-control-home" "$icd" "" 0 0 -- gamescope --backend headless -W 1280 -H 720 -- vkcube --c "$FRAMES"
    if ! why=$(exited_cleanly "$control"); then
        record SKIP "surface gamescope" "gamescope cannot run here without the layer either: $(grep -m1 -E 'Error|error' "$control" || echo "$why")"
        return
    fi
    lane_surface "$icd" "$dir" gamescope unvalidated -- gamescope --backend headless -W 1280 -H 720 -- env ENABLE_VKDAEDALUS=1 vkcube --c "$FRAMES"
}

wait_for_line() {
    local pattern="$1" want="$2" log="$3"
    until [ -f "$log" ] && [ "$(count "$pattern" "$log")" -ge "$want" ]; do
        if [ -f "$log.rc" ]; then return 1; fi
        sleep 0.1
    done
}

hold_key_until() {
    local key="$1" pattern="$2" want="$3" log="$4" status=0 seen=0
    if [ -f "$log" ]; then seen=$(count "$pattern" "$log"); fi
    DISPLAY="$PRIVATE_DISPLAY" xdotool keydown "$key"
    wait_for_line "$pattern" "$((seen + want))" "$log" || status=1
    DISPLAY="$PRIVATE_DISPLAY" xdotool keyup "$key"
    wait_for_line 'present cycle' $(($(count 'present cycle' "$log") + 2)) "$log" || true
    return "$status"
}

press_overlay_key() {
    wait_for_line 'present cycle' 30 "$1" || return 1
    hold_key_until "$OVERLAY_KEY" 'overlay opened' 1 "$1"
}

overlay_probe_case() {
    local icd="$1" dir="$2" name="$3" slug="$4" want="$5" why presser frames
    shift 5
    local log="$dir/overlay-$slug.log" control="$dir/overlay-$slug-control.log" home="$dir/overlay-$slug-home"
    run "$control" "$dir/overlay-$slug-control-home" "$icd" "$VALIDATION" 0 1 -- "$@"
    if [ "$(cat "$control.rc")" = 77 ]; then
        record SKIP "$name" "$(tail -1 "$control.stdout")"
        return
    fi
    if ! why=$(exited_cleanly "$control"); then
        record FAIL "$name: control" "$why"
        return
    fi
    fresh_home "$home"
    printf 'overlayKey = %s\n' "$OVERLAY_KEY" >"$home/.config/vkdaedalus/settings.conf"
    background press_overlay_key "$log"
    presser=$!
    run "$log" "$home" "$icd" "$BASALT:$VALIDATION" 1 1 -- "$@"
    frames=$(count 'overlay recorded a frame' "$log")
    if ! wait "$presser"; then
        record FAIL "$name" "the overlay never opened: the probe did not reach 30 presents, or the held key went unseen"
    elif ! why=$(exited_cleanly "$log"); then
        record FAIL "$name" "$why"
    elif ! why=$(validation_verdict "$log" "$control"); then
        record FAIL "$name: validation" "$why"
    elif [ "$(count 'vkDaedalus err:|ImGui fence wait timed out' "$log")" -ne 0 ]; then
        record FAIL "$name" "$(grep -m1 -E 'vkDaedalus err:|ImGui fence wait timed out' "$log")"
    elif ! grep -qE "$want" "$log.stdout"; then
        record FAIL "$name" "the probe did not report what the case needs: $(tail -1 "$log.stdout")"
    elif [ "$frames" -lt 60 ]; then
        record FAIL "$name" "the overlay drew $frames frames, want at least 60"
    else
        record PASS "$name" "$(tail -1 "$log.stdout" | sed -E 's/^[a-z-]+: //'), the overlay drew $frames frames under validation"
    fi
}

lane_overlay() {
    local icd="$1" dir="$2" why
    overlay_probe_case "$icd" "$dir" "overlay beside a second thread's queue" race \
        '^queue-race-probe: [0-9]+ presents on queue 1 while another thread made [1-9][0-9]* submits on queue 0$' queue-race-probe "$FRAMES"
    overlay_probe_case "$icd" "$dir" "overlay on two swapchains" pair \
        '^two-swapchain-probe: [0-9]+ presents alternating a format [0-9]+ and a format [0-9]+ swapchain on one device$' two-swapchain-probe "$FRAMES"

    local log="$dir/overlay.log" control="$dir/overlay-control.log" home="$dir/overlay-home" presser opened frames
    run "$control" "$dir/overlay-control-home" "$icd" "$VALIDATION" 0 1 -- vkcube --c "$FRAMES" --wsi xcb
    if ! why=$(exited_cleanly "$control"); then
        record FAIL "overlay: control without the layer" "$why"
        return
    fi
    fresh_home "$home"
    printf 'overlayKey = %s\n' "$OVERLAY_KEY" >"$home/.config/vkdaedalus/settings.conf"
    background press_overlay_key "$log"
    presser=$!
    run "$log" "$home" "$icd" "$BASALT:$VALIDATION" 1 1 -- vkcube --c "$FRAMES" --wsi xcb
    if ! wait "$presser"; then
        record FAIL "overlay opened by its key" "the overlay never opened: the app did not reach 30 presents, or the held key went unseen"
        return
    fi
    if ! why=$(exited_cleanly "$log"); then
        record FAIL "overlay opened by its key" "$why"
        return
    fi
    if ! why=$(validation_verdict "$log" "$control"); then
        record FAIL "overlay: validation" "$why"
        return
    fi
    opened=$(count 'overlay opened' "$log")
    frames=$(count 'overlay recorded a frame' "$log")
    if [ "$(count 'vkDaedalus err:' "$log")" -ne 0 ]; then
        record FAIL "overlay opened by its key" "$(grep -m1 'vkDaedalus err:' "$log")"
    elif [ "$opened" -ne 1 ] || [ "$frames" -lt 30 ]; then
        record FAIL "overlay opened by its key" "opened $opened time(s), recorded $frames overlay frames after pressing $OVERLAY_KEY"
    else
        record PASS "overlay opened by its key" "$OVERLAY_KEY over XTEST opened it once, $frames overlay frames under validation"
    fi
}

ui_line() {
    awk -F'\t' -v l="$2" -v w="${UI_WINDOW:-}" 'NR > 1 && $2 == l && (w == "" || $1 == w || index($1, w "/") == 1) { print; exit }' "$1" 2>/dev/null || true
}

ui_field() {
    awk -F'\t' -v l="$2" -v f="$3" -v w="${UI_WINDOW:-}" 'NR > 1 && $2 == l && (w == "" || $1 == w || index($1, w "/") == 1) { print $f; exit }' "$1" 2>/dev/null || true
}

ui_body_fits() {
    local map="$1" body="$2" card="$3" ui_log="$4" range
    range=$(awk -F'\t' -v b="/$body" '$2 == "(scroll range)" && index($1, b "_") { print $6 + 0; exit }' "$map")
    if [ -z "$range" ]; then
        ui_result "$ui_log" FAIL "$card fits without scrolling" "the item map has no scroll range for $body"
    elif [ "${range%.*}" -gt 0 ]; then
        ui_result "$ui_log" FAIL "$card fits without scrolling" "its body scrolls by $range px, so part of it is below the fold"
    else
        ui_result "$ui_log" PASS "$card fits without scrolling" "its body needs no scrolling"
    fi
}

ui_in_view() {
    local map="$1" body="$2" card="$3" ui_log="$4" want="$5" hidden
    hidden=$(awk -F'\t' -v b="/$body" -v want="$want" '
        BEGIN { n = split(want, labels, "|"); for (k = 1; k <= n; k++) wanted[labels[k]] = 1 }
        NR > 1 && index($1, b "_") && $1 == $2 { bottom = $6; next }
        NR > 1 && index($1, b "_") && ($2 in wanted) { if (!($2 in deepest) || $6 > deepest[$2]) deepest[$2] = $6 }
        END {
            for (k = 1; k <= n; k++)
                if (bottom == "" || !(labels[k] in deepest) || deepest[labels[k]] > bottom + 0.75) printf "%s%s", (shown++ ? ", " : ""), labels[k]
        }' "$map")
    if [ -z "$hidden" ]; then
        ui_result "$ui_log" PASS "$card keeps its key figures in view" "${want//|/, } sit inside its body"
    else
        ui_result "$ui_log" FAIL "$card keeps its key figures in view" "$hidden below the fold or missing"
    fi
}

drive_card_sizes() {
    local log="$1" map="$2" tag="$3" display="$4" suffix="$5" ui_log="$1.ui" window card wait body name
    shift 5
    : >"$ui_log"
    if ! wait_for_line 'present cycle' 30 "$log"; then
        ui_result "$ui_log" FAIL "the cards fit $display" "the game never reached 30 presents"
        return 0
    fi
    window=$(DISPLAY="$PRIVATE_DISPLAY" xdotool search --onlyvisible --name '' | tail -1) || true
    ui_take_window "$window"
    if ! hold_key_until "$OVERLAY_KEY" 'overlay opened' 1 "$log"; then
        ui_result "$ui_log" FAIL "the cards fit $display" "$OVERLAY_KEY was held but the overlay never opened"
        return 0
    fi
    for card in "$@"; do
        case "$card" in
            Performance) wait="Per-effect GPU timing" body="##performancebody" name=performance ;;
            GPUs) wait="##gpuname" body="##gpusbody" name=gpus ;;
            "Frame generation") wait="Game step" body="##framegenerationbody" name=frame-generation ;;
        esac
        ui_click "$map" "$card" 1 "$log" || true
        if [ "$card" = "Frame generation" ] && ! ui_wait "$map" "$wait" "" "" "$log"; then
            ui_click "$map" "On###frameGeneration" 1 "$log" || true
        fi
        if ! ui_wait "$map" "$wait" "" "" "$log"; then
            ui_result "$ui_log" FAIL "the $card card fits $display" "a click on $card never showed $wait"
            continue
        fi
        sleep 1
        ui_state "$map" "size-$tag-$name$suffix" "$ui_log" "the $card card at $display"
        if [ "$tag" != 720p ]; then
            ui_body_fits "$map" "$body" "the $card card at $display" "$ui_log"
        elif [ "$card" = GPUs ]; then
            ui_in_view "$map" "$body" "the GPUs card at $display" "$ui_log" "##gpuname"
        else
            ui_in_view "$map" "$body" "the $card card at $display" "$ui_log" "##frameintervals|Per-effect GPU timing"
        fi
    done
    ui_states_verdict "$ui_log"
    hold_key_until "$OVERLAY_KEY" 'overlay closed' 1 "$log" || true
}

ui_frame() {
    awk 'NR == 1 { print $2 + 0; exit }' "$1"
}

ui_wait() {
    local map="$1" label="$2" field="$3" want="$4" log="$5" tries=0
    while true; do
        if [ -n "$(ui_line "$map" "$label")" ] && { [ -z "$field" ] || [ "$(ui_field "$map" "$label" "$field")" = "$want" ]; }; then
            return 0
        fi
        if [ -f "$log.rc" ] || [ "$tries" -ge 100 ]; then return 1; fi
        tries=$((tries + 1))
        sleep 0.1
    done
}

ui_gone() {
    local map="$1" label="$2" log="$3" tries=0
    while [ -n "$(ui_line "$map" "$label")" ]; do
        if [ -f "$log.rc" ] || [ "$tries" -ge 100 ]; then return 1; fi
        tries=$((tries + 1))
        sleep 0.1
    done
}

ui_point() {
    awk -F'\t' -v l="$2" -v w="${UI_WINDOW:-}" '
        NR > 1 && $1 == $2 { top[$1] = $4; bottom[$1] = $6 }
        NR > 1 && !found && $2 == l && (w == "" || $1 == w || index($1, w "/") == 1) { found = 1; in_window = $1; x = ($3 + $5) / 2; y0 = $4; y1 = $6 }
        END { if (found && (in_window in top)) printf "%d %d %d %d %d\n", x, y0, y1, top[in_window], bottom[in_window] }' "$1" 2>/dev/null || true
}

ui_hover() {
    local map="$1" label="$2" log="$3" x y0 y1 top bottom window wheel tries=0 wx wy px py
    window=$(DISPLAY="$PRIVATE_DISPLAY" xdotool search --onlyvisible --name '' | tail -1) || return 1
    ui_wait "$map" "$label" "" "" "$log" || return 1
    while true; do
        read -r x y0 y1 top bottom <<<"$(ui_point "$map" "$label")"
        if [ -z "$bottom" ]; then return 1; fi
        if [ $(((y0 + y1) / 2)) -lt "$top" ]; then
            wheel=4
        elif [ $(((y0 + y1) / 2)) -ge "$bottom" ]; then
            wheel=5
        else
            break
        fi
        if [ "$tries" -ge 20 ]; then return 1; fi
        tries=$((tries + 1))
        DISPLAY="$PRIVATE_DISPLAY" xdotool mousemove --window "$window" "$x" "$(((top + bottom) / 2))" click "$wheel" || return 1
        sleep 0.3
    done
    if [ "$y0" -lt "$top" ]; then y0=$top; fi
    if [ "$y1" -gt "$bottom" ]; then y1=$bottom; fi
    DISPLAY="$PRIVATE_DISPLAY" xdotool mousemove --window "$window" "$x" "$(((y0 + y1) / 2))" || return 1
    sleep 0.2
    read -r wx wy <<<"$(DISPLAY="$PRIVATE_DISPLAY" xdotool getwindowgeometry --shell "$window" | awk -F= '$1 == "X" { x = $2 } $1 == "Y" { y = $2 } END { print x, y }')"
    read -r px py <<<"$(DISPLAY="$PRIVATE_DISPLAY" xdotool getmouselocation --shell | awk -F= '$1 == "X" { x = $2 } $1 == "Y" { y = $2 } END { print x, y }')"
    if [ "$(((px - wx - x) * (px - wx - x) + (py - wy - (y0 + y1) / 2) * (py - wy - (y0 + y1) / 2)))" -gt 8 ]; then
        echo "$label: sent to $x,$(((y0 + y1) / 2)) in the game window, the pointer stopped at $((px - wx)),$((py - wy)) (window at $wx,$wy)" >>"$log.pointer"
        return 1
    fi
}

ui_take_window() {
    if [ -z "$1" ]; then return 0; fi
    DISPLAY="$PRIVATE_DISPLAY" xdotool windowmove --sync "$1" 0 0 || true
    DISPLAY="$PRIVATE_DISPLAY" xdotool windowfocus --sync "$1" || true
}

ui_click() {
    local map="$1" label="$2" button="$3" log="$4"
    if [ -f "$log.rc" ]; then
        echo "$label: the game had already ended, its UI_FRAMES ran out before the driver did" >>"$log.pointer"
        return 1
    fi
    ui_hover "$map" "$label" "$log" || return 1
    local seen before
    before=$(ui_frame "$map")
    DISPLAY="$PRIVATE_DISPLAY" xdotool mousedown "$button" || return 1
    sleep 0.2
    seen=$(awk -v b="$button" 'NR == 1 { split("6 8 7", field, " "); print $1 " " $2 ": mouse " $4 "," $5 ", button " b " down " $(field[b]); exit }' "$map" 2>/dev/null) || seen=""
    if [[ "$seen" != *" down 1" ]]; then echo "$label: button $button held 0.2 s from frame $before, the overlay's latest ${seen:-map could not be read}" >>"$log.pointer"; fi
    DISPLAY="$PRIVATE_DISPLAY" xdotool mouseup "$button" || return 1
    sleep 0.3
}

gallery_shot() {
    local map="$1" name="$2" shot="$1.$2.xwd" box window x0 y0 x1 y1
    if [ -z "$GALLERY_PARTIAL" ] || [[ "$map" != "$WORK/gpu-1/"* ]]; then return 0; fi
    sleep 0.3
    box=$(awk -F'\t' 'NR > 1 && ($1 == "vkDaedalus" || $1 == "vkDaedalus notice") && $2 == $1 { printf "%d %d %d %d", ($3 < 0 ? 0 : $3), ($4 < 0 ? 0 : $4), $5, $6; exit }' "$map")
    window=$(DISPLAY="$PRIVATE_DISPLAY" xdotool search --onlyvisible --name '' | tail -1) || window=""
    if [ -z "$box" ] || [ -z "$window" ]; then
        echo "gallery: $name not captured: no item map or no game window" >&2
        return 0
    fi
    read -r x0 y0 x1 y1 <<<"$box"
    if ! xwd -silent -display "$PRIVATE_DISPLAY" -id "$window" -out "$shot" ||
        ! xwdtopnm "$shot" | pamcut -pad -left "$x0" -top "$y0" -right "$((x1 - 1))" -bottom "$((y1 - 1))" | pnmtopng >"$GALLERY_PARTIAL/$name.png"; then
        echo "gallery: $name not captured: the game window could not be read" >&2
        rm -f "$GALLERY_PARTIAL/$name.png" "$shot"
        return 0
    fi
    rm -f "$shot"
    local previous="$GALLERY_LAST"
    GALLERY_LAST="$GALLERY_PARTIAL/$name.png"
    if [ "${GALLERY_SAME:-0}" = 0 ] && [ -n "$previous" ] && cmp -s "$previous" "$GALLERY_LAST"; then
        echo "gallery: $name is the same picture as $(basename "$previous" .png)" >&2
        return 1
    fi
}

gallery_index() {
    local map="$1" name="$2" ui_log="$3" caption="$4" results
    if [ -z "$GALLERY_PARTIAL" ] || [[ "$map" != "$WORK/gpu-1/"* ]]; then return 0; fi
    if [ -z "$caption" ] && [ "$(count '' "$ui_log")" -le "$GALLERY_SEEN" ]; then
        ui_result "$ui_log" FAIL "the gallery says what each picture shows" "$name has no caption and no check recorded since the picture before it"
    fi
    results=$(count '' "$ui_log")
    awk -F'\t' -v from="$GALLERY_SEEN" -v name="$name" -v caption="$caption" '
        NR > from { failed = failed || $1 == "FAIL"; last = $1; checks = checks (checks == "" ? "" : " | ") $2; seen = seen (seen == "" ? "" : " | ") $3 }
        END {
            verdict = failed ? "FAIL" : (caption != "" ? "VIEW" : last)
            if (caption != "") checks = caption (checks == "" ? "" : " | " checks)
            printf "%s\t%s\t%s\t%s\n", name, verdict, checks, seen
        }' "$ui_log" >>"$GALLERY_PARTIAL/index.tsv"
    GALLERY_SEEN=$results
}

ui_layout_problem() {
    awk -F'\t' '
        NR == 1 { next }
        $2 == "(scroll range)" { if (($1 == "vkDaedalus" || $1 == "vkDaedalus card") && $6 > 0) scrolls = $6; next }
        {
            n++; win[n] = $1; lab[n] = $2; x0[n] = $3; y0[n] = $4; x1[n] = $5; y1[n] = $6
            if ($1 == $2) { wx0[$1] = $3; wx1[$1] = $5 }
        }
        function inFrame(w) { return w == "vkDaedalus" || w == "vkDaedalus card" || index(w, "vkDaedalus card/") == 1 }
        function inCard(w) { return w == "vkDaedalus card" || index(w, "vkDaedalus card/") == 1 }
        function nearMiss(a, b) { d = a - b; if (d < 0) d = -d; return d > 0.75 && d <= 3 }
        function aligned(a, b) { d = a - b; if (d < 0) d = -d; return d <= 0.75 }
        END {
            if (scrolls > 0) {
                printf "the panel scrolls by %d px, so this page does not fit the frame", scrolls
                exit
            }
            scale = 0
            for (i = 1; i <= n; i++) if (win[i] == "vkDaedalus" && lab[i] == "Close##frame") scale = (y1[i] - y0[i]) / 32
            split("26 32 40 52", kit, " ")
            for (i = 1; i <= n && scale > 0; i++) {
                if (!(inCard(win[i]) || index(win[i], "##Popup_") == 1 || index(win[i], "##Combo_") == 1) || lab[i] == win[i] || lab[i] == "(an item past the window edge)" || lab[i] == "##footer") continue
                if (lab[i] == "##frameintervals") continue
                fits = 0
                for (k in kit) { d = (y1[i] - y0[i]) - kit[k] * scale; if (d >= -0.6 && d <= 0.6) fits = 1 }
                if (!fits) {
                    printf "\"%s\" in %s is %.1f px high, not a kit height (26, 32, 40 or 52 at scale %.2f)", lab[i], win[i], y1[i] - y0[i], scale
                    exit
                }
            }
            for (i = 1; i <= n; i++) {
                if (!inFrame(win[i]) || lab[i] == win[i]) continue
                for (j = i + 1; j <= n; j++) {
                    if (win[j] != win[i] || lab[j] == win[j]) continue
                    if (!aligned(x1[i], x1[j]) && nearMiss(x0[i], x0[j])) { printf "\"%s\" and \"%s\" in %s start %.1f and %.1f px across: almost aligned, not aligned", lab[i], lab[j], win[i], x0[i], x0[j]; exit }
                    if (!aligned(x0[i], x0[j]) && (win[i] in wx1) && wx1[win[i]] - x1[i] < 48 && wx1[win[i]] - x1[j] < 48 && nearMiss(x1[i], x1[j])) { printf "\"%s\" and \"%s\" in %s end %.1f and %.1f px across: almost aligned, not aligned", lab[i], lab[j], win[i], x1[i], x1[j]; exit }
                    top = y0[i] > y0[j] ? y0[i] : y0[j]; bottom = y1[i] < y1[j] ? y1[i] : y1[j]
                    shorter = (y1[i] - y0[i]) < (y1[j] - y0[j]) ? (y1[i] - y0[i]) : (y1[j] - y0[j])
                    across = x0[j] > x1[i] ? x0[j] - x1[i] : (x0[i] > x1[j] ? x0[i] - x1[j] : 0)
                    if (bottom - top > shorter / 2 && across < 24 * (scale > 0 ? scale : 1) - 0.75 && nearMiss((y0[i] + y1[i]) / 2, (y0[j] + y1[j]) / 2)) {
                        printf "\"%s\" and \"%s\" in %s share a line but their centres are %.1f and %.1f px down", lab[i], lab[j], win[i], (y0[i] + y1[i]) / 2, (y0[j] + y1[j]) / 2
                        exit
                    }
                }
            }
            for (i = 1; i <= n && scale > 0; i++) {
                if (!inCard(win[i]) || lab[i] == win[i] || lab[i] == "##footer") continue
                best = -1
                for (j = 1; j <= n; j++) {
                    if (j == i || win[j] != win[i] || lab[j] == win[j] || lab[j] == "##footer") continue
                    top = y0[i] > y0[j] ? y0[i] : y0[j]; bottom = y1[i] < y1[j] ? y1[i] : y1[j]
                    shorter = (y1[i] - y0[i]) < (y1[j] - y0[j]) ? (y1[i] - y0[i]) : (y1[j] - y0[j])
                    if (bottom - top <= shorter / 2 || x0[j] < x1[i] - 0.75) continue
                    if (best < 0 || x0[j] < x0[best]) best = j
                }
                if (best < 0) continue
                g = x0[best] - x1[i]
                if (g < 24 * scale - 0.75 && (g - 8 * scale > 0.75 || 8 * scale - g > 0.75) && (g - 16 * scale > 0.75 || 16 * scale - g > 0.75)) {
                    printf "\"%s\" and \"%s\" in %s sit %.1f px apart on one line, not the kit gap of 8 or 16 at scale %.2f", lab[i], lab[best], win[i], g, scale
                    exit
                }
            }
            for (i = 1; i <= n; i++) {
                if (win[i] == "vkDaedalus card" && lab[i] != win[i]) cardItems++
                if (win[i] == "vkDaedalus card" && lab[i] == "##footer") footers++
            }
            if (cardItems > 0 && footers == 0) {
                printf "the card has no footer"
                exit
            }
            for (i = 1; i <= n; i++) {
                w = win[i]
                if (lab[i] == w || !(w in wx0)) continue
                if (index(w, "##filebrowser_") > 0) continue
                if (x0[i] < wx0[w] - 1 || x1[i] > wx1[w] + 1) {
                    printf "\"%s\" spans x %d to %d, outside its window %s at %d to %d", lab[i], x0[i], x1[i], w, wx0[w], wx1[w]
                    exit
                }
            }
            for (i = 1; i <= n; i++) {
                at = index(win[i], "###Menu_")
                if (win[i] != lab[i] || at == 0) continue
                opener = substr(win[i], 1, at - 1)
                for (j = 1; j <= n; j++) {
                    if (lab[j] != opener || win[j] == win[i]) continue
                    cx = (x0[j] + x1[j]) / 2; cy = (y0[j] + y1[j]) / 2
                    if (cx >= x0[i] && cx <= x1[i] && cy >= y0[i] && cy <= y1[i]) {
                        printf "the menu \"%s\" covers the item that opened it", opener
                        exit
                    }
                }
            }
        }' "$1"
}

ui_settled() {
    local map="$1" log="$2" start tries=0
    start=$(ui_frame "$map")
    while [ "$(ui_frame "$map")" -lt $((start + 3)) ]; do
        if [ -f "$log.rc" ] || [ "$tries" -ge 100 ]; then return 1; fi
        tries=$((tries + 1))
        sleep 0.1
    done
}

ui_state() {
    local map="$1" name="$2" ui_log="$3" caption="${4:-}" problem
    ui_settled "$map" "${ui_log%.ui}" || echo "$name: the overlay drew fewer than 3 frames after its action, so the state was judged unsettled" >>"${ui_log%.ui}.pointer"
    if ! problem=$(ui_layout_problem "$map"); then problem="the item map at $map could not be read"; fi
    if [ -n "$problem" ]; then ui_result "$ui_log" FAIL "the overlay keeps its controls in view" "$name: $problem"; fi
    echo "$name" >>"$ui_log.states"
    if ! gallery_shot "$map" "$name"; then
        ui_result "$ui_log" FAIL "the gallery shows each state" "$name came out as the same picture as the state before it, so the capture read an old frame"
    fi
    gallery_index "$map" "$name" "$ui_log" "$caption"
}

ui_states_verdict() {
    local ui_log="$1" states
    states=$(count '' "$ui_log.states")
    if [ "$states" -gt 0 ] && [ "$(count '^FAIL	the overlay keeps its controls in view	' "$ui_log")" -eq 0 ]; then
        ui_result "$ui_log" PASS "the overlay keeps its controls in view" "$states states: every item inside its window, no menu over the item that opened it, every card control at a kit height, nothing almost aligned"
    fi
}

ui_type() {
    local text="$1" i c
    for ((i = 0; i < ${#text}; i++)); do
        c=${text:i:1}
        case "$c" in
            .) c=period ;;
            -) c=minus ;;
            _) c=underscore ;;
            " ") c=space ;;
        esac
        ui_key "$c" || return 1
    done
}

ui_set_value() {
    local map="$1" label="$2" value="$3" log="$4" i
    ui_hover "$map" "$label" "$log" || return 1
    DISPLAY="$PRIVATE_DISPLAY" xdotool keydown ctrl || return 1
    sleep 0.1
    DISPLAY="$PRIVATE_DISPLAY" xdotool mousedown 1 || return 1
    sleep 0.2
    DISPLAY="$PRIVATE_DISPLAY" xdotool mouseup 1 || return 1
    DISPLAY="$PRIVATE_DISPLAY" xdotool keyup ctrl || return 1
    sleep 0.3
    for i in 1 2 3 4 5 6 7 8; do ui_key BackSpace || return 1; done
    ui_type "$value" || return 1
    ui_key Return
}

ui_key() {
    DISPLAY="$PRIVATE_DISPLAY" xdotool keydown "$1" || return 1
    sleep 0.1
    DISPLAY="$PRIVATE_DISPLAY" xdotool keyup "$1" || return 1
    sleep 0.1
}

file_says() {
    local file="$1" pattern="$2" tries=0
    until [ -f "$file" ] && grep -qE -- "$pattern" "$file"; do
        if [ "$tries" -ge 100 ]; then return 1; fi
        tries=$((tries + 1))
        sleep 0.1
    done
}

file_holds() {
    local file="$1" text="$2" tries=0
    until [ -f "$file" ] && grep -qF -- "$text" "$file"; do
        if [ "$tries" -ge 100 ]; then return 1; fi
        tries=$((tries + 1))
        sleep 0.1
    done
}

ui_result() {
    local map="${1%.log.ui}.map"
    printf '%s\t%s\t%s\n' "$2" "$3" "$4" >>"$1"
    if [ "$2" = FAIL ] && [ -f "$map" ] && ! cat "$map" >"$map.fail-$(count '^FAIL	' "$1")"; then
        echo "ui_result: the item map $map could not be kept beside the failure" >&2
    fi
}

drive_ui() {
    local log="$1" home="$2" map="$3" ui_log="$1.ui" window configs="$2/.config/vkdaedalus/configs" profiles="$2/.config/vkdaedalus/profiles" pid environ leaked reads width tree label stale safe
    : >"$ui_log"
    if ! wait_for_line 'present cycle' 30 "$log"; then
        ui_result "$ui_log" FAIL "the game ran" "it never reached 30 presents"
        return 0
    fi
    if ! pid=$(vkcube_of_run "$home") || ! environ=$(tr '\0' '\n' <"/proc/$pid/environ"); then
        ui_result "$ui_log" FAIL "the game gets only the variables its run names" "the environment of the vkcube with HOME=$home could not be read"
    elif leaked=$(comm -23 <(cut -d= -f1 <<<"$environ" | sort -u) "$log.env") && [ -n "$leaked" ]; then
        ui_result "$ui_log" FAIL "the game gets only the variables its run names" "vkcube's environment also holds $(tr '\n' ' ' <<<"$leaked")"
    else
        ui_result "$ui_log" PASS "the game gets only the variables its run names" "vkcube's environment holds the $(wc -l <"$log.env") variables its run names and no other"
    fi
    window=$(DISPLAY="$PRIVATE_DISPLAY" xdotool search --onlyvisible --name '' | tail -1) || true
    ui_take_window "$window"
    if ! hold_key_until "$OVERLAY_KEY" 'overlay opened' 1 "$log"; then
        ui_result "$ui_log" FAIL "the overlay opened" "$OVERLAY_KEY was held but the overlay never opened"
        return 0
    fi
    if ! ui_wait "$map" "Safe Anti-Cheat" "" "" "$log"; then
        ui_result "$ui_log" FAIL "the item map" "no map naming the Safe Anti-Cheat checkbox appeared at $map"
        return 0
    fi
    ui_state "$map" ui-01-effects "$ui_log" "the overlay opened on the Effects card"
    width=$(awk -F'\t' 'NR > 1 && $1 == "vkDaedalus" && $2 == "vkDaedalus" { printf "%d %d %d %d", $3, $4, $5 - $3, $6 - $4; exit }' "$map")
    if [ "$width" = "0 0 1280 720" ]; then
        ui_result "$ui_log" PASS "the overlay covers the frame" "a window saved 400 px wide in imgui.ini opened over the whole 1280 by 720 frame"
    else
        ui_result "$ui_log" FAIL "the overlay covers the frame" "it opened at x, y, width, height ${width:-unknown}, want 0 0 1280 720"
    fi

    ui_click "$map" "##profile" 1 "$log" || true
    stale=$(ui_label_starting "$map" "stale (" "$log") || stale=""
    safe=$(ui_label_starting "$map" "safe (" "$log") || safe=""
    if [ -z "$stale" ] || [ -z "$safe" ]; then
        ui_result "$ui_log" FAIL "profile menu" "the menu never listed the stale and safe profiles"
    elif [ "$(ui_field "$map" "$stale" 7)" != 0 ] || [ "$(ui_field "$map" "$safe" 7)" != 0 ]; then
        ui_result "$ui_log" FAIL "profile menu" "stale disabled $(ui_field "$map" "$stale" 7), safe disabled $(ui_field "$map" "$safe" 7), want 0 and 0: a profile holds no Safe Anti-Cheat to grey it for"
    else
        ui_state "$map" ui-02-profile-menu "$ui_log" "the profile menu, each profile with the number of games that run it"
        ui_click "$map" "$stale" 1 "$log" || true
        if ! wait_for_line 'switching to config: .*/profiles/stale\.conf' 1 "$log"; then
            ui_result "$ui_log" FAIL "profile menu" "a click on stale never switched to it"
        elif ! ui_wait "$map" "Safe Anti-Cheat" 8 1 "$log" || ! file_says "$configs/vkcube.conf" '^safeAntiCheat = true$' || [ "$(count 'Safe Anti-Cheat is off' "$log")" -ne 0 ]; then
            ui_result "$ui_log" FAIL "profile menu" "switching to stale, whose file still says safeAntiCheat = false, turned Safe Anti-Cheat off: ticked $(ui_field "$map" "Safe Anti-Cheat" 8), $(grep -m1 'Safe Anti-Cheat is off' "$log" || echo 'nothing said off')"
        else
            ui_click "$map" "##profile" 1 "$log" || true
            safe=$(ui_label_starting "$map" "safe (" "$log") || safe=""
            ui_click "$map" "$safe" 1 "$log" || true
            if wait_for_line 'switching to config: .*/profiles/safe\.conf' 1 "$log"; then
                ui_result "$ui_log" PASS "profile menu" "every profile listed with its games and none greyed; stale, whose file still says safeAntiCheat = false, ran with Safe Anti-Cheat on; safe switched"
            else
                ui_result "$ui_log" FAIL "profile menu" "a click on the safe profile never switched to it"
            fi
        fi
    fi
    ui_state "$map" ui-02b-profile-switched "$ui_log"

    if [ "$(ui_field "$map" "Safe Anti-Cheat" 8)" != 1 ]; then
        ui_result "$ui_log" FAIL "Safe Anti-Cheat checkbox" "it was not ticked for a game whose own file says safeAntiCheat = true"
    else
        ui_click "$map" "Safe Anti-Cheat" 1 "$log" || true
        if ! ui_wait "$map" "Safe Anti-Cheat" 8 0 "$log"; then
            ui_result "$ui_log" FAIL "Safe Anti-Cheat checkbox" "a click did not untick it"
        elif ! file_says "$configs/vkcube.conf" '^safeAntiCheat = false$'; then
            ui_result "$ui_log" FAIL "Safe Anti-Cheat checkbox" "unticking it never reached vkcube's own file, configs/vkcube.conf"
        elif file_says "$profiles/safe.conf" '^safeAntiCheat'; then
            ui_result "$ui_log" FAIL "Safe Anti-Cheat checkbox" "unticking it wrote safeAntiCheat into the profile safe, which other games may run"
        else
            ui_state "$map" ui-02c-safe-anticheat "$ui_log" "Safe Anti-Cheat unticked by a click and saved into the game's own file"
            ui_click "$map" "Safe Anti-Cheat" 1 "$log" || true
            if ! ui_wait "$map" "Safe Anti-Cheat" 8 1 "$log" || ! file_says "$configs/vkcube.conf" '^safeAntiCheat = true$'; then
                ui_result "$ui_log" FAIL "Safe Anti-Cheat checkbox" "ticking it again did not reach the checkbox and vkcube's own file"
            else
                ui_result "$ui_log" PASS "Safe Anti-Cheat checkbox" "unticked and ticked again, each saved into vkcube's own file and never into the profile"
            fi
        fi
    fi

    if ui_wait "$map" "texture_too_wide (left out on this GPU)" "" "" "$log"; then
        ui_result "$ui_log" PASS "a GPU's refusal shows" "the effect this GPU cannot run is listed as left out on this GPU"
    else
        ui_result "$ui_log" FAIL "a GPU's refusal shows" "texture_too_wide was never listed as left out on this GPU"
    fi
    ui_state "$map" ui-02d-refused-effect "$ui_log"

    ui_click "$map" "macro_gated_parameter" 1 "$log" || true
    if ! ui_wait "$map" "macro_gated_parameter" 9 1 "$log"; then
        ui_result "$ui_log" FAIL "a definition shows its parameter" "a click on the macro_gated_parameter row never expanded it"
    elif [ -n "$(ui_line "$map" "Gate Blue")" ]; then
        ui_result "$ui_log" FAIL "a definition shows its parameter" "Gate Blue was listed while USE_GATE was still 0"
    else
        ui_click "$map" "Definitions" 1 "$log" || true
        reads=$(count 'read the parameters of macro_gated_parameter again' "$log")
        ui_click "$map" "##USE_GATE" 1 "$log" || true
        if ! file_says "$log" 'preprocessor definition USE_GATE of macro_gated_parameter set to 1$'; then
            ui_result "$ui_log" FAIL "a definition shows its parameter" "switching USE_GATE on never reached the definition"
        elif ! ui_wait "$map" "Waits for Apply" "" "" "$log"; then
            ui_result "$ui_log" FAIL "a definition shows its parameter" "USE_GATE switched on, but the footer never said Waits for Apply"
        elif ! { ui_click "$map" "Apply" 1 "$log" && wait_for_count 'read the parameters of macro_gated_parameter again' $((reads + 1)) "$log"; }; then
            ui_result "$ui_log" FAIL "a definition shows its parameter" "USE_GATE set to 1 and applied never read the parameters again"
        elif ! ui_wait "$map" "Gate Blue" "" "" "$log"; then
            ui_result "$ui_log" FAIL "a definition shows its parameter" "the parameters were read again but Gate Blue never appeared"
        elif ! { sleep 2 && ui_gone "$map" "Waits for Apply" "$log"; }; then
            ui_result "$ui_log" FAIL "a definition shows its parameter" "USE_GATE was applied, but the footer still said Waits for Apply"
        else
            ui_result "$ui_log" PASS "a definition shows its parameter" "USE_GATE switched on showed Waits for Apply; applied, it read the parameters again, Gate Blue appeared and the footer stopped waiting"
            ui_state "$map" ui-03-definition "$ui_log" "USE_GATE switched on and applied, so Gate Blue appears"
        fi
    fi

    ui_click "$map" "cas" 1 "$log" || true
    if ! ui_wait "$map" "cas" 9 1 "$log"; then
        ui_result "$ui_log" FAIL "remove an expanded effect" "a click on the cas row never expanded it"
    else
        ui_click "$map" "cas" 3 "$log" || true
        ui_wait "$map" "Remove" "" "" "$log" || true
        ui_state "$map" ui-04-row-menu "$ui_log" "an effect's row menu, opened with a right click"
        if [ "$(ui_field "$map" "Move up" 7)" = 1 ]; then
            ui_hover "$map" "Move up" "$log" || true
            sleep 1
            GALLERY_SAME=1 ui_state "$map" ui-04a-move-up-greyed "$ui_log" "the pointer held over the greyed Move up of the first row"
        fi
        ui_click "$map" "Remove" 1 "$log" || true
        sleep 1
        if [ -f "$log.rc" ]; then
            ui_result "$ui_log" FAIL "remove an expanded effect" "the game ended when an expanded effect was removed (issue 26)"
        elif [ "$(count 'recovered from an ImGui error' "$log")" -ne 0 ]; then
            ui_result "$ui_log" FAIL "remove an expanded effect" "$(grep -m1 'recovered from an ImGui error' "$log")"
        elif ! ui_gone "$map" "cas" "$log"; then
            ui_result "$ui_log" FAIL "remove an expanded effect" "the cas row was still listed after Remove"
        else
            ui_result "$ui_log" PASS "remove an expanded effect" "the expanded cas row was removed and the game ran on, with no ImGui error"
        fi
    fi
    ui_state "$map" ui-04b-effect-removed "$ui_log"

    ui_click "$map" "Add effects..." 1 "$log" || true
    if ! ui_wait "$map" "##search" "" "" "$log"; then
        ui_result "$ui_log" FAIL "add effects" "Add effects... never showed the effect list"
    else
        ui_click "$map" "rcas" 1 "$log" || true
        ui_type macro || true
        ui_state "$map" ui-05-add-effects "$ui_log" "Add effects with rcas chosen and macro typed into the search"
        if ! ui_click "$map" "macro_spacing" 1 "$log"; then
            ui_click "$map" "Cancel" 1 "$log" || true
            ui_result "$ui_log" FAIL "add effects" "the effect list never showed macro_spacing, even with macro typed into its search"
        else
            ui_click "$map" "Add 2 effects###addchosen" 1 "$log" || true
            if ! { wait_for_line 'creating effect [0-9]+: rcas' 1 "$log" && wait_for_line 'creating effect [0-9]+: macro_spacing' 1 "$log"; }; then
                ui_result "$ui_log" FAIL "add effects" "rcas built $(count 'creating effect [0-9]+: rcas' "$log") times, macro_spacing $(count 'creating effect [0-9]+: macro_spacing' "$log"), want each at least once"
            elif [ "$(count 'compiling .*macro_spacing.fx with the layer lock held' "$log")" -ne 0 ]; then
                ui_result "$ui_log" FAIL "add effects" "with a global definition set, macro_spacing compiled with the layer lock held, so frames waited for it"
            else
                ui_result "$ui_log" PASS "add effects" "rcas, and macro_spacing found through the list's search, joined the chain after their background compile, with a global definition set and no compile under the layer lock"
            fi
            local active saved
            active=$(sed -n 's/^vkcube=//p' "$configs/.active_profiles" 2>/dev/null | tail -1)
            saved="$profiles/${active:-default}.conf"
            if file_says "$saved" '^effects = .*rcas'; then
                ui_result "$ui_log" PASS "added effects are saved" "rcas reached $(basename "$saved"), the active profile, with no other edit"
            else
                ui_result "$ui_log" FAIL "added effects are saved" "rcas ran but never reached $(basename "$saved"), the active profile"
            fi
        fi
    fi
    ui_state "$map" ui-05b-effects-added "$ui_log"

    ui_click "$map" "##profile" 1 "$log" || true
    ui_click "$map" "New...##newprofile" 1 "$log" || true
    if ! ui_wait "$map" "##newprofname" "" "" "$log"; then
        ui_result "$ui_log" FAIL "new profile" "the profile menu's New... never opened the new profile popup"
    else
        ui_click "$map" "##newprofname" 1 "$log" || true
        ui_type default || true
        ui_click "$map" "Create" 1 "$log" || true
        if ! file_says "$log" "new profile refused: 'default' and 'none' are taken by vkDaedalus$"; then
            ui_result "$ui_log" FAIL "a taken profile name is refused with the reason" "Create with default never said why it made nothing"
        else
            ui_result "$ui_log" PASS "a taken profile name is refused with the reason" "Create with default made nothing and said 'default' and 'none' are taken by vkDaedalus"
        fi
        ui_state "$map" ui-06a-name-refused "$ui_log"
        ui_click "$map" "##newprofname" 1 "$log" || true
        for _ in 1 2 3 4 5 6 7; do ui_key BackSpace || true; done
        ui_type made || true
        ui_state "$map" ui-06-new-profile "$ui_log" "New profile with a free name typed"
        ui_click "$map" "Create" 1 "$log" || true
        if ! wait_for_line 'switching to config: .*/profiles/made\.conf' 1 "$log"; then
            ui_result "$ui_log" FAIL "new profile" "Create never switched the layer to profiles/made.conf"
        elif ! file_says "$profiles/made.conf" '^effects = ' || file_says "$profiles/made.conf" '^safeAntiCheat' || ! file_says "$configs/.active_profiles" '^vkcube=made$'; then
            ui_result "$ui_log" FAIL "new profile" "profiles/made.conf is missing, has no effects line or holds a safeAntiCheat line, or vkcube does not run it: $(grep -m1 '^vkcube=' "$configs/.active_profiles" || echo no vkcube entry)"
        else
            ui_result "$ui_log" PASS "new profile" "New... made profiles/made.conf from the current profile, with no Safe Anti-Cheat of its own, and vkcube switched to it"
        fi
    fi
    ui_state "$map" ui-06b-profile-made "$ui_log"

    ui_click "$map" "##profile" 1 "$log" || true
    ui_click "$map" "Delete made...##delprofile" 1 "$log" || true
    ui_wait "$map" "Yes, delete" "" "" "$log" || true
    ui_state "$map" ui-07-delete-profile "$ui_log" "the confirmation before the made profile is deleted"
    ui_click "$map" "Yes, delete" 1 "$log" || true
    if wait_for_line 'switching to config: .*/profiles/default\.conf' 1 "$log" && [ ! -e "$profiles/made.conf" ] && ! file_says "$configs/.active_profiles" '^vkcube=made$'; then
        ui_result "$ui_log" PASS "delete profile" "the made profile's file was deleted and vkcube went back to default"
    else
        ui_result "$ui_log" FAIL "delete profile" "profiles/made.conf exists: $([ -e "$profiles/made.conf" ] && echo yes || echo no); vkcube's entry: $(grep -m1 '^vkcube=' "$configs/.active_profiles" || echo none); switches to default: $(count 'switching to config: .*/profiles/default\.conf' "$log")"
    fi
    ui_state "$map" ui-07b-profile-deleted "$ui_log"

    ui_click "$map" "Shaders" 1 "$log" || true
    ui_click "$map" "Test all shaders" 1 "$log" || true
    if wait_for_line 'Shader test started in the background' 1 "$log" && wait_for_line 'Shader test complete' 1 "$log"; then
        ui_state "$map" ui-08-shader-test "$ui_log" "the shader test's results"
        for label in "Failed" "Blocked" "Allowed" "no_technique"; do
            tree=$(ui_label_starting "$map" "$label" "$log") || tree=""
            ui_click "$map" "${tree:-$label}" 1 "$log" || true
            if [ -n "$tree" ] && ui_wait "$map" "$tree" 9 1 "$log"; then
                ui_result "$ui_log" PASS "the shader test's $label... list opens" "$tree opened"
            else
                ui_result "$ui_log" FAIL "the shader test's $label... list opens" "no entry starting with $label opened"
            fi
            case "$label" in
                "Failed") ui_state "$map" ui-08a-failed-list "$ui_log" ;;
                "Blocked") ui_state "$map" ui-08b-blocked-list "$ui_log" ;;
                "Allowed") ui_state "$map" ui-08c-allowed-list "$ui_log" ;;
                *) ui_state "$map" ui-08d-no-technique "$ui_log" ;;
            esac
        done
        ui_click "$map" "Effects" 1 "$log" || true
        ui_click "$map" "Add effects..." 1 "$log" || true
        ui_type depth || true
        if ui_wait "$map" "depth_semantic_smoke" 7 1 "$log"; then
            ui_result "$ui_log" PASS "Add effects: greyed when it reads depth" "after the shader test found depth_semantic_smoke reading depth, it could not be added with Safe Anti-Cheat on"
        else
            ui_result "$ui_log" FAIL "Add effects: greyed when it reads depth" "depth_semantic_smoke was still offered with Safe Anti-Cheat on"
        fi
        ui_state "$map" ui-08e-depth-greyed "$ui_log"
        ui_click "$map" "Cancel" 1 "$log" || true
        ui_click "$map" "Shaders" 1 "$log" || true
        ui_result "$ui_log" PASS "shader test" "Test All Shaders ran on its own thread and completed while the game drew"
    else
        ui_result "$ui_log" FAIL "shader test" "started $(count 'Shader test started in the background' "$log"), completed $(count 'Shader test complete' "$log")"
    fi

    ui_click "$map" "Settings" 1 "$log" || true
    ui_state "$map" ui-09-settings "$ui_log" "the Settings card"
    if [ "$(ui_field "$map" "Depth capture" 7)" = 1 ]; then
        ui_result "$ui_log" PASS "Depth capture is greyed while Safe Anti-Cheat is on" "the checkbox is disabled in a profile with Safe Anti-Cheat on"
    else
        ui_result "$ui_log" FAIL "Depth capture is greyed while Safe Anti-Cheat is on" "the checkbox could be clicked with Safe Anti-Cheat on"
    fi
    if [ "$(ui_field "$map" "##launchNotice" 7)" = 1 ]; then
        ui_result "$ui_log" PASS "Settings: Launch notice is held by VKDAEDALUS_LAUNCH_NOTICE" "the lanes run with VKDAEDALUS_LAUNCH_NOTICE=never and its menu is greyed"
    else
        ui_result "$ui_log" FAIL "Settings: Launch notice is held by VKDAEDALUS_LAUNCH_NOTICE" "with VKDAEDALUS_LAUNCH_NOTICE=never the Launch notice menu was $( [ -n "$(ui_line "$map" "##launchNotice")" ] && echo open to change || echo missing)"
    fi
    ui_click "$map" "Performance" 1 "$log" || true
    if ui_wait "$map" "Per-effect GPU timing" "" "" "$log"; then
        ui_result "$ui_log" PASS "the Performance page" "a click on Performance showed its page"
    else
        ui_result "$ui_log" FAIL "the Performance page" "a click on Performance never showed its page"
    fi
    ui_state "$map" ui-10-performance "$ui_log" "the Performance card"
    ui_in_view "$map" "##performancebody" "the Performance card at 1280x720" "$ui_log" "##frameintervals|Per-effect GPU timing"
    ui_click "$map" "GPUs" 1 "$log" || true
    local busy=0 graphs reading
    for reading in /sys/class/drm/card[0-9]*/device/gpu_busy_percent; do
        if [ -r "$reading" ]; then busy=$((busy + 1)); fi
    done
    if [ "$busy" -eq 0 ]; then
        ui_result "$ui_log" SKIP "GPU readings for every GPU" "no GPU here reports gpu_busy_percent"
    else
        sleep 1
        graphs=$(awk -F'\t' 'NR > 1 && index($2, "##gpuusage-") == 1 { print $2 }' "$map" | sort -u | wc -l)
        if [ "$graphs" -ge "$busy" ]; then
            ui_result "$ui_log" PASS "GPU readings for every GPU" "$graphs usage meters for the $busy GPUs whose driver reports how busy they are"
        else
            ui_result "$ui_log" FAIL "GPU readings for every GPU" "$graphs usage meters, though $busy GPUs report how busy they are"
        fi
    fi
    local memory=0 memoryGraphs
    for reading in /sys/class/drm/card[0-9]*/device/mem_info_vram_total; do
        if [ -r "$reading" ]; then memory=$((memory + 1)); fi
    done
    if [ "$memory" -eq 0 ]; then
        ui_result "$ui_log" SKIP "video memory readings for every GPU" "no GPU here reports mem_info_vram_total"
    else
        sleep 1
        memoryGraphs=$(awk -F'\t' 'NR > 1 && index($2, "##vramusage-") == 1 { print $2 }' "$map" | sort -u | wc -l)
        if [ "$memoryGraphs" -ge "$memory" ]; then
            ui_result "$ui_log" PASS "video memory readings for every GPU" "$memoryGraphs video memory meters for the $memory GPUs whose driver reports their memory"
        else
            ui_result "$ui_log" FAIL "video memory readings for every GPU" "$memoryGraphs video memory meters, though $memory GPUs report their memory"
        fi
    fi
    ui_state "$map" ui-11-gpus "$ui_log" "the GPUs card with every GPU's readings"
    ui_in_view "$map" "##gpusbody" "the GPUs card at 1280x720" "$ui_log" "##gpuname"
    ui_states_verdict "$ui_log"

    closed=$(count 'overlay closed' "$log")
    grabbed=$(count 'Input grabbed for overlay' "$log")
    released=$(count 'Input released from overlay' "$log")
    ui_click "$map" "Close##frame" 1 "$log" || true
    wait_for_line 'present cycle' $(($(count 'present cycle' "$log") + 30)) "$log" || true
    if [ "$(count "the overlay's close button was clicked" "$log")" -lt 1 ] || [ "$(count 'overlay closed' "$log")" -le "$closed" ]; then
        ui_result "$ui_log" FAIL "the overlay's close button" "a click on the close button never closed the overlay"
        hold_key_until "$OVERLAY_KEY" 'overlay closed' 1 "$log" || true
    elif [ "$grabbed" -gt "$released" ] && [ "$(count 'Input released from overlay' "$log")" -le "$released" ]; then
        ui_result "$ui_log" FAIL "the overlay's close button" "the close button closed the overlay but left input grabbed"
    else
        ui_result "$ui_log" PASS "the overlay's close button" "a click on the close button closed the overlay through the overlay key's path, input released"
    fi
}

restart_mark_says() {
    local map="$1" what="$2" want="$3" log="$4" ui_log="$5" name="$6" why="$7" got
    if ui_wait "$map" "$what restart mark" 8 "$want" "$log"; then
        if [ "$want" = 1 ]; then
            ui_result "$ui_log" PASS "$name" "$why, its restart mark shows the change waits for the next start"
        else
            ui_result "$ui_log" PASS "$name" "$why, its restart mark shows nothing waits"
        fi
    else
        got=$(ui_field "$map" "$what restart mark" 8)
        ui_result "$ui_log" FAIL "$name" "$why, its restart mark says ${got:-nothing, it is not on the page}, want $want"
    fi
}

lsfg_profile_value() {
    awk -v key="$2" '/^\[\[profile\]\]/ { here = 0 } $0 == "name = \"vkdaedalus default\"" { here = 1 } here && $1 == key { print $3 }' "$1"
}

start_only_flips() {
    local map="$1" label="$2" key="$3" config="$4" log="$5" ui_log="$6" was want
    was=$(lsfg_profile_value "$config" "$key")
    if [ "$was" = true ]; then want=false; else want=true; fi
    ui_click "$map" "$label" 1 "$log" || true
    if [ "$(lsfg_profile_value "$config" "$key")" = "$want" ]; then
        restart_mark_says "$map" "$label" 1 "$log" "$ui_log" "Frame generation: $label waits for the next start" "a click saved $key = $want in vkcube's lsfg-vk profile"
    else
        ui_result "$ui_log" FAIL "Frame generation: $label waits for the next start" "a click left $key at $(lsfg_profile_value "$config" "$key"), want $want"
    fi
}

drive_next_start_frame_generation() {
    local log="$1" map="$2" config="$3" ui_log="$1.ui" window
    : >"$ui_log"
    if ! wait_for_line 'present cycle' 30 "$log"; then
        ui_result "$ui_log" FAIL "frame generation's start-only options" "the game never reached 30 presents"
        return 0
    fi
    window=$(DISPLAY="$PRIVATE_DISPLAY" xdotool search --onlyvisible --name '' | tail -1) || true
    ui_take_window "$window"
    if ! hold_key_until "$OVERLAY_KEY" 'overlay opened' 1 "$log"; then
        ui_result "$ui_log" FAIL "frame generation's start-only options" "$OVERLAY_KEY was held but the overlay never opened"
        return 0
    fi
    ui_click "$map" "Frame generation" 1 "$log" || true
    restart_mark_says "$map" "Half precision" 0 "$log" "$ui_log" "Frame generation: Half precision applies at the next start" "the run before turned it off and this run started with allow_fp16 = $(sed -n 's/^allow_fp16 = //p' "$config")"
    ui_click "$map" "On###frameGeneration" 1 "$log" || true
    if ! ui_wait "$map" "Force vsync (FIFO)" "" "" "$log"; then
        ui_result "$ui_log" FAIL "frame generation's start-only options" "On never showed Force vsync (FIFO): $(lsfg_profile_value "$config" multiplier)x"
        return 0
    fi
    start_only_flips "$map" "Force vsync (FIFO)" override_present_mode "$config" "$log" "$ui_log"
    start_only_flips "$map" "Keep swapchain image count" preserve_swapchain_image_count "$config" "$log" "$ui_log"
    restart_mark_says "$map" "Effects after frame generation" 1 "$log" "$ui_log" "Frame generation: effects after it waits for vkdaedalus-run" "saved in the run before, this run was started without vkdaedalus-run, so its order is the one the layers were given"
    ui_state "$map" fg-08-start-only "$ui_log" "the next start: the start-only options marked"
    local listed=""
    ui_click "$map" "##profile" 1 "$log" || true
    ui_click "$map" "New...##newprofile" 1 "$log" || true
    ui_click "$map" "##newprofname" 1 "$log" || true
    ui_type split || true
    ui_click "$map" "Create" 1 "$log" || true
    if ! wait_for_line 'switching to config: .*/profiles/split\.conf' 1 "$log"; then
        ui_result "$ui_log" FAIL "frame generation follows the profile" "Create with split never switched vkcube to profiles/split.conf"
        return 0
    fi
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        listed=$(lsfg_profile_value "$config" active_in)
        if [ "$listed" = "[]" ]; then break; fi
        sleep 0.5
    done
    if [ "$listed" = "[]" ]; then
        ui_result "$ui_log" PASS "frame generation follows the profile" "vkcube switched to a new profile left the lsfg-vk profile of default listing no game, so its frame generation follows split from its next launch"
    else
        ui_result "$ui_log" FAIL "frame generation follows the profile" "after vkcube switched to split, default's lsfg-vk profile still lists ${listed:-nothing it could read}"
    fi
}

drive_next_start_settings() {
    local log="$1" map="$2" ui_log="$1.ui" window
    : >"$ui_log"
    if ! wait_for_line 'present cycle' 30 "$log"; then
        ui_result "$ui_log" FAIL "Settings: Depth capture applies at the next start" "the game never reached 30 presents"
        return 0
    fi
    window=$(DISPLAY="$PRIVATE_DISPLAY" xdotool search --onlyvisible --name '' | tail -1) || true
    ui_take_window "$window"
    if ! hold_key_until "$OVERLAY_KEY" 'overlay opened' 1 "$log"; then
        ui_result "$ui_log" FAIL "Settings: Depth capture applies at the next start" "$OVERLAY_KEY was held but the overlay never opened"
        return 0
    fi
    ui_click "$map" "Settings" 1 "$log" || true
    restart_mark_says "$map" "Depth capture" 0 "$log" "$ui_log" "Settings: Depth capture applies at the next start" "turned on in the run before"
}

settings_click_flips() {
    local map="$1" label="$2" key="$3" on="$4" off="$5" file="$6" log="$7" ui_log="$8" was want
    if ! ui_wait "$map" "$label" "" "" "$log"; then
        ui_result "$ui_log" FAIL "Settings: $label" "$label was not on the page, so it could not be clicked"
        return 0
    fi
    was=$(ui_field "$map" "$label" 8)
    if [ "$was" = 1 ]; then want=$off; else want=$on; fi
    if ! ui_click "$map" "$label" 1 "$log"; then
        ui_result "$ui_log" FAIL "Settings: $label" "a click on $label never landed"
        return 0
    fi
    if ! ui_wait "$map" "$label" 8 "$((1 - ${was:-0}))" "$log"; then
        ui_result "$ui_log" FAIL "Settings: $label" "a click left $label ticked: ${was:-unknown}"
    elif file_says "$file" "^$key = $want\$"; then
        ui_result "$ui_log" PASS "Settings: $label" "a click wrote $key = $want into settings.conf"
    else
        ui_result "$ui_log" FAIL "Settings: $label" "a click never wrote $key = $want into settings.conf (it was ticked: ${was:-unknown})"
    fi
}

settings_rebinds_key() {
    local map="$1" button="$2" key="$3" pressed="$4" file="$5" log="$6" ui_log="$7" fired="$8" state="${9:-}" before
    ui_click "$map" "$button" 1 "$log" || true
    if ! ui_wait "$map" "Press a key..." "" "" "$log"; then
        ui_result "$ui_log" FAIL "Settings: the $key button" "a click on $button never started listening for a key"
        return 0
    fi
    if [ -n "$state" ]; then ui_state "$map" "$state" "$ui_log" "a click on $button: the key button listens for the next key"; fi
    before=$(count "$fired" "$log")
    ui_key "$pressed" || true
    sleep 0.5
    if ! file_says "$file" "^$key = $pressed\$"; then
        ui_result "$ui_log" FAIL "Settings: the $key button" "a click on $button, then $pressed, never wrote $key = $pressed"
    elif [ "$(count "$fired" "$log")" -ne "$before" ]; then
        ui_result "$ui_log" FAIL "Settings: the $key button" "the $pressed press that bound $key also ran it: the log says $fired"
    else
        ui_result "$ui_log" PASS "Settings: the $key button" "a click on $button, then $pressed, wrote $key = $pressed without running it"
    fi
}

drive_settings() {
    local log="$1" home="$2" map="$3" ui_log="$1.ui" window settings="$2/.config/vkdaedalus/settings.conf" label grabs was
    : >"$ui_log"
    if ! wait_for_line 'present cycle' 30 "$log"; then
        ui_result "$ui_log" FAIL "the Settings page" "the game never reached 30 presents"
        return 0
    fi
    window=$(DISPLAY="$PRIVATE_DISPLAY" xdotool search --onlyvisible --name '' | tail -1) || true
    ui_take_window "$window"
    if ! hold_key_until "$OVERLAY_KEY" 'overlay opened' 1 "$log"; then
        ui_result "$ui_log" FAIL "the Settings page" "$OVERLAY_KEY was held but the overlay never opened"
        return 0
    fi
    ui_click "$map" "Settings" 1 "$log" || true
    if ! ui_wait "$map" "Instant ReShade sliders" "" "" "$log"; then
        ui_result "$ui_log" FAIL "the Settings page" "a click on Settings never showed its controls"
        return 0
    fi
    ui_state "$map" ui-12-compact-1024x768 "$ui_log" "the compact layout on a 1024x768 game"
    grabs=$(count 'Input grabbed for overlay' "$log")
    if [ "$grabs" -ge 1 ] && ! grep -q '^overlayBlockInput' "$settings"; then
        ui_result "$ui_log" PASS "the open overlay keeps the game's input" "opening it grabbed the keyboard and pointer, with no setting asked"
    else
        ui_result "$ui_log" FAIL "the open overlay keeps the game's input" "with the overlay open, $grabs grabs were logged and settings.conf says $(grep -m1 '^overlayBlockInput' "$settings" || echo nothing)"
    fi
    settings_click_flips "$map" "Effects on at launch" enableOnLaunch true false "$settings" "$log" "$ui_log"
    was=$(ui_field "$map" "Effects on at launch" 8)
    if ui_hover "$map" "Effects on at launch" "$log" && DISPLAY="$PRIVATE_DISPLAY" xdotool click 1 && ui_wait "$map" "Effects on at launch" 8 "$((1 - ${was:-0}))" "$log"; then
        ui_result "$ui_log" PASS "a click shorter than a frame reaches the overlay" "a press and release sent at once flipped Effects on at launch back"
    else
        ui_result "$ui_log" FAIL "a click shorter than a frame reaches the overlay" "a press and release sent at once left Effects on at launch ticked: ${was:-unknown}"
    fi
    settings_click_flips "$map" "Depth capture" depthCapture on off "$settings" "$log" "$ui_log"
    restart_mark_says "$map" "Depth capture" 1 "$log" "$ui_log" "Settings: Depth capture waits for the next start" "turned on while this run started without it"
    ui_click "$map" "Performance" 1 "$log" || true
    settings_click_flips "$map" "Per-effect GPU timing" effectGpuTiming true false "$settings" "$log" "$ui_log"
    ui_state "$map" set-01-gpu-timing "$ui_log"
    ui_click "$map" "Settings" 1 "$log" || true
    settings_click_flips "$map" "Instant ReShade sliders" liveReshadeUniforms true false "$settings" "$log" "$ui_log"
    ui_set_value "$map" "##maxEffects" 12 "$log" || true
    if file_says "$settings" '^maxEffects = 12$'; then
        ui_result "$ui_log" PASS "Settings: Most effects in the chain" "12 typed into it reached settings.conf"
    else
        ui_result "$ui_log" FAIL "Settings: Most effects in the chain" "12 typed into it never reached settings.conf"
    fi
    ui_state "$map" set-02-effect-limit "$ui_log"
    if [ "$(ui_field "$map" "Effect limit default mark" 8)" != 1 ]; then
        ui_result "$ui_log" FAIL "Settings: a changed value is marked" "with the limit at 12 its default mark says $(ui_field "$map" "Effect limit default mark" 8 || echo nothing), want changed"
    elif ! ui_click "$map" "Effect limit default mark" 1 "$log" || ! ui_wait "$map" "Effect limit default mark" 8 0 "$log" || ! file_says "$settings" '^maxEffects = 10$'; then
        ui_result "$ui_log" FAIL "Settings: a changed value is marked" "a click on the mark never set the limit back to its default, 10: $(grep -m1 '^maxEffects' "$settings")"
    else
        ui_result "$ui_log" PASS "Settings: a changed value is marked" "the limit at 12 was marked changed, and a click on the mark set it back to the default 10"
    fi
    ui_click "$map" "##overlaySize" 1 "$log" || true
    if ui_wait "$map" "100%" "" "" "$log"; then
        ui_result "$ui_log" PASS "Settings: the Size menu" "a click listed the sizes"
    else
        ui_result "$ui_log" FAIL "Settings: the Size menu" "a click never listed the sizes"
    fi
    ui_state "$map" set-03-size-menu "$ui_log"
    ui_click "$map" "##overlaySize" 1 "$log" || true
    if ! ui_gone "$map" "100%" "$log"; then
        ui_result "$ui_log" FAIL "Settings: the Size menu" "a second click on it never closed the menu"
    fi
    for label in "Apply automatically" "Apply automatically"; do
        settings_click_flips "$map" "$label" autoApply true false "$settings" "$log" "$ui_log"
    done
    ui_set_value "$map" "##autoApplyDelay" 350 "$log" || true
    if file_says "$settings" '^autoApplyDelay = 350$'; then
        ui_result "$ui_log" PASS "Settings: the auto-apply delay" "350 typed into it reached settings.conf"
    else
        ui_result "$ui_log" FAIL "Settings: the auto-apply delay" "350 typed into it never reached settings.conf"
    fi
    ui_state "$map" set-04-auto-apply "$ui_log"
    ui_click "$map" "Keys" 1 "$log" || true
    ui_wait "$map" "Home" "" "" "$log" || true
    ui_state "$map" keys-01-card "$ui_log" "the Keys card"
    settings_rebinds_key "$map" Home toggleKey F6 "$settings" "$log" "$ui_log" 'effects (on|off)$' keys-02-listening
    settings_rebinds_key "$map" F10 reloadKey F7 "$settings" "$log" "$ui_log" 'reload key pressed$'
    settings_rebinds_key "$map" "$OVERLAY_KEY" overlayKey F12 "$settings" "$log" "$ui_log" 'overlay closed$'
    ui_state "$map" keys-03-rebound "$ui_log"
    ui_click "$map" "F6" 1 "$log" || true
    ui_key F7 || true
    sleep 1
    if grep -qx 'toggleKey = F6' "$settings"; then
        ui_result "$ui_log" PASS "Settings: a key another action uses" "F7, the reload key, pressed for Toggle effects left it on F6"
    else
        ui_result "$ui_log" FAIL "Settings: a key another action uses" "F7, the reload key, pressed for Toggle effects changed it to $(grep -m1 '^toggleKey' "$settings")"
    fi
    ui_state "$map" keys-04-key-taken "$ui_log"
    ui_click "$map" "Debug" 1 "$log" || true
    drive_debug_window "$log" "$home" "$map" "$ui_log"
    local historyOff
    historyOff=$(count 'debug log history off' "$log")
    if hold_key_until F12 'overlay closed' 1 "$log" && wait_for_line 'debug log history off' "$((historyOff + 1))" "$log"; then
        ui_result "$ui_log" PASS "the debug log stops with the overlay" "closing the overlay on the Debug card switched its history off"
    else
        ui_result "$ui_log" FAIL "the debug log stops with the overlay" "closing the overlay on the Debug card left its history recording every log call"
    fi
    hold_key_until F12 'overlay opened' 1 "$log" || true
    ui_click "$map" "Settings" 1 "$log" || true
    ui_click "$map" "##hud" 1 "$log" || true
    if ! ui_wait "$map" "FPS only" "" "" "$log"; then
        ui_result "$ui_log" FAIL "Settings: the HUD" "a click on the HUD menu never listed Steam's levels"
    else
        ui_state "$map" set-05-hud-menu "$ui_log" "the HUD menu: Off, Steam's three levels and a switch per item"
        ui_click "$map" "FPS only" 1 "$log" || true
        if ! file_says "$settings" '^hud = true$' || ! file_says "$settings" '^hudItems = fps$'; then
            ui_result "$ui_log" FAIL "Settings: the HUD" "FPS only never saved hud = true with hudItems = fps: $(grep -m2 '^hud' "$settings" | tr '\n' ' ')"
        elif ! hold_key_until F12 'overlay closed' 1 "$log" || ! wait_for_line 'HUD shown while the overlay is closed: 1 line \(fps\)$' 1 "$log"; then
            ui_result "$ui_log" FAIL "Settings: the HUD" "with FPS only saved, closing the overlay drew no HUD"
        else
            ui_result "$ui_log" PASS "Settings: the HUD" "FPS only saved hud = true with hudItems = fps, and the HUD drew once the overlay closed"
        fi
    fi
    hold_key_until F12 'overlay opened' 1 "$log" || true
    ui_click "$map" "##palette" 1 "$log" || true
    if ! ui_wait "$map" "Steel" "" "" "$log"; then
        ui_result "$ui_log" FAIL "Settings: the colour scheme" "a click on Colours never listed the schemes"
    else
        ui_state "$map" set-06-colours-menu "$ui_log" "the colour schemes, Daaboulex soft first"
        ui_click "$map" "Steel" 1 "$log" || true
        if file_says "$settings" '^palette = steel$'; then
            ui_state "$map" set-07-steel "$ui_log" "the overlay drawn in the Steel scheme"
            ui_result "$ui_log" PASS "Settings: the colour scheme" "Steel saved palette = steel and the overlay drew in it"
        else
            ui_result "$ui_log" FAIL "Settings: the colour scheme" "Steel never reached settings.conf"
        fi
        ui_click "$map" "##palette" 1 "$log" || true
        ui_click "$map" "Daaboulex, soft" 1 "$log" || true
    fi
    ui_states_verdict "$ui_log"
}

drive_debug_window() {
    local log="$1" home="$2" map="$3" ui_log="$4" UI_WINDOW="vkDaedalus card" label was before after
    local exported
    if ! ui_wait "$map" "##logLevel" "" "" "$log"; then
        ui_result "$ui_log" FAIL "the debug window" "the Debug tile never opened the Debug card"
        return 0
    fi
    if [ "$(ui_field "$map" "##logLevel" 7)" = 1 ]; then
        ui_result "$ui_log" PASS "the debug window: the log level" "its menu is held, as VKDAEDALUS_LOG_LEVEL sets the level for this run"
    else
        ui_result "$ui_log" FAIL "the debug window: the log level" "with VKDAEDALUS_LOG_LEVEL set, the log level menu was $( [ -n "$(ui_line "$map" "##logLevel")" ] && echo open to change || echo missing)"
    fi
    ui_state "$map" dbg-01-window "$ui_log"
    ui_click "$map" "Frame path" 1 "$log" || true
    if ui_wait "$map" "Frame path" 8 1 "$log"; then
        local strip
        strip=$(awk -F'\t' 'NR > 1 && index($1, "vkDaedalus card") == 1 && $2 ~ / step$/ { printf "%s%s", (n++ ? ", " : ""), $2 }' "$map")
        if [ "$strip" = "Game step, Effects step, Display step" ]; then
            ui_result "$ui_log" PASS "the Frame path tab: the run's steps" "the strip shows the game, the effects and the display, in that order"
        else
            ui_result "$ui_log" FAIL "the Frame path tab: the run's steps" "the strip's steps were: ${strip:-none}"
        fi
        ui_state "$map" dbg-06-frame-path "$ui_log" "the Frame path tab: this run's steps, the layers below vkDaedalus, the image use and which figures are measured"
    else
        ui_result "$ui_log" FAIL "the debug window's Frame path tab" "a click on Frame path never selected it"
    fi
    ui_click "$map" "Paths" 1 "$log" || true
    if ui_wait "$map" "Paths" 8 1 "$log"; then
        ui_state "$map" dbg-07-paths "$ui_log" "the Paths tab: the program and every file vkDaedalus reads or writes for it"
    else
        ui_result "$ui_log" FAIL "the debug window's Paths tab" "a click on Paths never selected it"
    fi
    ui_click "$map" "Effects" 1 "$log" || true
    ui_click "$map" "cas" 1 "$log" || true
    ui_click "$map" "macro_gated_parameter" 1 "$log" || true
    if ! ui_wait "$map" "macro_gated_parameter" 9 1 "$log"; then
        ui_result "$ui_log" FAIL "the debug window's Effects tab" "the macro_gated_parameter entry never opened"
    else
        label=$(ui_label_starting "$map" "Parameters" "$log") || true
        ui_click "$map" "${label:-Parameters}" 1 "$log" || true
        ui_click "$map" "Definitions" 1 "$log" || true
        if [ -n "$label" ] && ui_wait "$map" "$label" 9 1 "$log" && ui_wait "$map" "Definitions" 9 1 "$log"; then
            ui_result "$ui_log" PASS "the debug window's Effects tab" "cas's registry entry opened its $label and macro_gated_parameter's its Definitions"
        else
            ui_result "$ui_log" FAIL "the debug window's Effects tab" "an entry's parameters (${label:-not listed}) or definitions never opened"
        fi
    fi
    ui_state "$map" dbg-02-effects-tab "$ui_log"
    ui_click "$map" "Log" 1 "$log" || true
    if ! ui_wait "$map" "Trace" "" "" "$log"; then
        ui_result "$ui_log" FAIL "the debug window's Log tab" "a click on Log never showed its filters"
        return 0
    fi
    for label in Trace Debug Info Warn Error; do
        was=$(ui_field "$map" "$label" 8)
        ui_click "$map" "$label" 1 "$log" || true
        if ui_wait "$map" "$label" 8 "$((1 - ${was:-0}))" "$log"; then
            ui_result "$ui_log" PASS "the debug log's $label filter" "a click switched it from ${was:-0} to $((1 - ${was:-0}))"
        else
            ui_result "$ui_log" FAIL "the debug log's $label filter" "a click did not switch it from ${was:-unknown}"
        fi
    done
    ui_state "$map" dbg-03-log-filters "$ui_log"
    ui_type zzz || true
    if ! ui_wait "$map" "##logsearch" "" "" "$log"; then
        ui_result "$ui_log" FAIL "the debug log's search" "typing zzz never brought up the search field"
    else
        ui_state "$map" dbg-04-log-search "$ui_log" "typing zzz brought up the log's search"
        ui_click "$map" "x" 1 "$log" || true
        if ui_gone "$map" "##logsearch" "$log"; then
            ui_result "$ui_log" PASS "the debug log's search" "typing zzz brought up the search and its x cleared it"
        else
            ui_result "$ui_log" FAIL "the debug log's search" "the x never cleared the search"
        fi
    fi
    ui_click "$map" "Export" 1 "$log" || true
    wait_for_line 'report exported to ' 1 "$log" || true
    exported=$(sed -n 's/.*report exported to //p' "$log" | tail -1)
    if [ -z "$exported" ] || [ ! -s "$exported" ]; then
        ui_result "$ui_log" FAIL "the debug log's Export" "Export never wrote a report: $(grep -m1 'the report could not be written' "$log" || echo 'no line said where')"
    elif [[ "$exported" != "$home/Downloads/vkdaedalus-report-vkcube-"* ]] || ! grep -qx 'Game: vkcube' "$exported" || ! grep -qx '== vkdaedalus.log, the last 2 MiB' "$exported" ||
        ! grep -q '^vkDaedalus info: ' "$exported"; then
        ui_result "$ui_log" FAIL "the debug log's Export" "the report $exported is not in ~/Downloads or lacks the game, the log file section or its lines"
    else
        ui_result "$ui_log" PASS "the debug log's Export" "it wrote one report to ~/Downloads with the game, its profile, the GPUs, the layers below and the log file's lines"
    fi
    ui_click "$map" "Clear" 1 "$log" || true
    if wait_for_line "the debug card's log was cleared: [1-9][0-9]* lines dropped" 1 "$log"; then
        ui_result "$ui_log" PASS "the debug log's Clear" "$(grep -m1 -o "the debug card's log was cleared: [0-9]* lines dropped" "$log")"
    else
        ui_result "$ui_log" FAIL "the debug log's Clear" "Clear never said it dropped the lines the card held"
    fi
    ui_state "$map" dbg-05-log-cleared "$ui_log"
}

drive_shaders() {
    local log="$1" home="$2" map="$3" ui_log="$1.ui" window conf="$2/.config/vkdaedalus/shader_manager.conf" profile="$2/.config/vkdaedalus/profiles/default.conf" label
    : >"$ui_log"
    if ! wait_for_line 'present cycle' 30 "$log"; then
        ui_result "$ui_log" FAIL "the Shaders page" "the game never reached 30 presents"
        return 0
    fi
    window=$(DISPLAY="$PRIVATE_DISPLAY" xdotool search --onlyvisible --name '' | tail -1) || true
    ui_take_window "$window"
    if ! hold_key_until "$OVERLAY_KEY" 'overlay opened' 1 "$log"; then
        ui_result "$ui_log" FAIL "the Shaders page" "$OVERLAY_KEY was held but the overlay never opened"
        return 0
    fi
    ui_click "$map" "Shaders" 1 "$log" || true
    if ! ui_wait "$map" "Browse..." "" "" "$log"; then
        ui_result "$ui_log" FAIL "the Shaders page" "a click on Shaders never showed Browse..."
        return 0
    fi
    ui_click "$map" "Browse..." 1 "$log" || true
    if ! ui_wait "$map" "cancel" "" "" "$log"; then
        ui_result "$ui_log" FAIL "Shaders: Browse..." "Browse... never opened the folder browser"
    else
        ui_state "$map" sh-01-folder-browser "$ui_log" "Browse... opened the folder browser"
        ui_click "$map" "cancel" 1 "$log" || true
        if ui_gone "$map" "cancel" "$log"; then
            ui_result "$ui_log" PASS "Shaders: Browse..." "it opened the folder browser and its cancel closed it"
        else
            ui_result "$ui_log" FAIL "Shaders: Browse..." "the folder browser's cancel never closed it"
        fi
    fi
    ui_click "$map" "Rescan all" 1 "$log" || true
    if file_says "$log" 'Shader Manager: Found 1 shader paths, 1 texture paths' && file_says "$conf" '^shaderPath = .*/pack/Shaders$' &&
        file_says "$conf" '^texturePath = .*/pack/Textures$'; then
        ui_result "$ui_log" PASS "Shaders: Rescan all" "it found the pack's Shaders and Textures folders and saved them"
    else
        ui_result "$ui_log" FAIL "Shaders: Rescan all" "it never found and saved the pack's Shaders and Textures folders"
    fi
    if file_says "$log" 'Shader Manager: .*-locked/pack cannot be read'; then
        ui_result "$ui_log" PASS "Shaders: an unreadable pack folder" "Rescan all named the pack folder under a folder it may not enter and went on"
    else
        ui_result "$ui_log" FAIL "Shaders: an unreadable pack folder" "Rescan all never said the pack folder under a folder it may not enter cannot be read"
    fi
    ui_state "$map" sh-02-rescanned "$ui_log"
    for label in "Shader folders" "Texture folders"; do
        ui_click "$map" "$label" 1 "$log" || true
        ui_wait "$map" "$label" 9 1 "$log" || true
    done
    ui_state "$map" sh-03-folders-open "$ui_log" "Shader folders and Texture folders opened"
    for label in shader texture; do
        ui_click "$map" "X##$label" 1 "$log" || true
        if ! grep -q "^${label}Path = " "$conf" 2>/dev/null && file_says "$log" 'Shader paths changed, effect list refreshed'; then
            ui_result "$ui_log" PASS "Shaders: the $label folder's X" "it took the $label folder out of shader_manager.conf"
        else
            sleep 1
            if grep -q "^${label}Path = " "$conf"; then
                ui_result "$ui_log" FAIL "Shaders: the $label folder's X" "the ${label}Path line stayed in shader_manager.conf"
            else
                ui_result "$ui_log" PASS "Shaders: the $label folder's X" "it took the $label folder out of shader_manager.conf"
            fi
        fi
    done
    ui_state "$map" sh-04-folders-removed "$ui_log"
    ui_click "$map" "Rescan all" 1 "$log" || true
    file_says "$conf" '^shaderPath = .*/pack/Shaders$' || true
    ui_click "$map" "X##pack" 1 "$log" || true
    ui_click "$map" "X##pack" 1 "$log" || true
    sleep 1
    if grep -q '^parentDir = ' "$conf"; then
        ui_result "$ui_log" FAIL "Shaders: the pack folder's X" "the parentDir line stayed in shader_manager.conf"
    elif grep -qE '^(shaderPath|texturePath) = .*/pack/(Shaders|Textures)$' "$conf"; then
        ui_result "$ui_log" FAIL "Shaders: the pack folder's X" "the pack folder went, but the Shaders and Textures folders found in it stayed"
    else
        ui_result "$ui_log" PASS "Shaders: the pack folder's X" "it took the pack folder out of shader_manager.conf"
    fi
    ui_state "$map" sh-05-pack-removed "$ui_log"
    ui_click "$map" "Effects" 1 "$log" || true
    label=$(ui_label_starting "$map" "Definitions for every effect" "$log") || true
    ui_click "$map" "${label:-Definitions for every effect}" 1 "$log" || true
    if ! ui_wait "$map" "RESHADE_DEPTH_INPUT_IS_REVERSED name" "" "" "$log"; then
        ui_result "$ui_log" FAIL "Effects: definitions for every effect" "the Effects card's definitions for every effect never opened to show RESHADE_DEPTH_INPUT_IS_REVERSED"
        return 0
    fi
    ui_state "$map" fx-21-definitions "$ui_log" "Definitions for every effect opened, on the Effects card"
    local nameEnd valueStart
    nameEnd=$(ui_field "$map" "RESHADE_DEPTH_INPUT_IS_REVERSED name" 5)
    valueStart=$(ui_field "$map" "##RESHADE_DEPTH_INPUT_IS_REVERSED" 3)
    if [ -n "$nameEnd" ] && [ -n "$valueStart" ] && awk -v a="$nameEnd" -v b="$valueStart" 'BEGIN { exit !(a < b) }'; then
        ui_result "$ui_log" PASS "Effects: a definition's name ends before its value" "RESHADE_DEPTH_INPUT_IS_REVERSED ends at $nameEnd px and its value starts at $valueStart px"
    else
        ui_result "$ui_log" FAIL "Effects: a definition's name ends before its value" "RESHADE_DEPTH_INPUT_IS_REVERSED ends at ${nameEnd:-nothing} px and its value starts at ${valueStart:-nothing} px"
    fi
    ui_click "$map" "RESHADE_DEPTH_INPUT_IS_REVERSED default mark" 1 "$log" || true
    if ! file_says "$log" 'global definition RESHADE_DEPTH_INPUT_IS_REVERSED set to 1$'; then
        ui_result "$ui_log" FAIL "Effects: a global definition's switch and default mark" "saved as 0, its default mark never set it back to global_definition.fx's 1"
    else
        ui_click "$map" "##RESHADE_DEPTH_INPUT_IS_REVERSED" 1 "$log" || true
        if file_says "$log" 'global definition RESHADE_DEPTH_INPUT_IS_REVERSED set to 0$'; then
            ui_result "$ui_log" PASS "Effects: a global definition's switch and default mark" "saved as 0, its default mark set it back to global_definition.fx's 1 and its switch turned it off again"
        else
            ui_result "$ui_log" FAIL "Effects: a global definition's switch and default mark" "back at its default 1, a click on its switch never turned it off"
        fi
    fi
    ui_state "$map" fx-22-definition-reset "$ui_log"
    ui_click "$map" "##globalname" 1 "$log" || true
    ui_type 1BAD || true
    ui_click "$map" "##globalvalue" 1 "$log" || true
    ui_type 3 || true
    ui_click "$map" "Add##globaldefinition" 1 "$log" || true
    if file_says "$log" 'global definition refused: '; then
        ui_result "$ui_log" PASS "Effects: a malformed global definition is refused" "$(grep -m1 -o 'global definition refused: .*' "$log")"
    else
        ui_result "$ui_log" FAIL "Effects: a malformed global definition is refused" "Add with the name 1BAD said nothing"
    fi
    ui_state "$map" fx-23-definition-refused "$ui_log"
    ui_click "$map" "##globalname" 1 "$log" || true
    for _ in 1 2 3 4; do ui_key BackSpace || true; done
    ui_type VKB_TEST || true
    ui_click "$map" "Add##globaldefinition" 1 "$log" || true
    if ! ui_wait "$map" "VKB_TEST" "" "" "$log"; then
        ui_result "$ui_log" FAIL "Effects: Add a global definition" "Add with VKB_TEST and 3 never listed VKB_TEST"
        return 0
    fi
    ui_state "$map" fx-24-definition-added "$ui_log" "VKB_TEST = 3 added to the global definitions"
    ui_click "$map" "Apply" 1 "$log" || true
    if file_says "$profile" '^@VKB_TEST = 3$'; then
        ui_result "$ui_log" PASS "Effects: Add a global definition" "VKB_TEST = 3 was listed and, applied, saved in the profile"
    else
        ui_result "$ui_log" FAIL "Effects: Add a global definition" "VKB_TEST was listed but Apply never saved @VKB_TEST = 3"
    fi
    ui_click "$map" "x##VKB_TEST" 1 "$log" || true
    if ui_gone "$map" "VKB_TEST" "$log"; then
        ui_result "$ui_log" PASS "Effects: Remove a global definition" "its x took VKB_TEST off the list"
    else
        ui_result "$ui_log" FAIL "Effects: Remove a global definition" "VKB_TEST was still listed after Remove"
    fi
    ui_state "$map" fx-25-definition-removed "$ui_log"
    ui_click "$map" "Shaders" 1 "$log" || true
    ui_click "$map" "Clear compiled effects" 1 "$log" || true
    if file_says "$log" 'compiled effects: cleared [0-9]+ entries from '; then
        ui_result "$ui_log" PASS "Shaders: Clear compiled effects" "$(grep -m1 -oE 'compiled effects: cleared [0-9]+ entries' "$log")"
    else
        ui_result "$ui_log" FAIL "Shaders: Clear compiled effects" "a click never cleared the cache"
    fi
    ui_state "$map" sh-11-cache-cleared "$ui_log"
}

wait_until_gone() {
    local file="$1" pattern="$2" tries=0
    while [ -f "$file" ] && grep -qE -- "$pattern" "$file"; do
        if [ "$tries" -ge 100 ]; then return 1; fi
        tries=$((tries + 1))
        sleep 0.1
    done
}

param_reaches_profile() {
    local profile="$1" pattern="$2" what="$3" ui_log="$4"
    if file_says "$profile" "$pattern"; then
        ui_result "$ui_log" PASS "Effects: $what" "the value reached the profile: $(grep -m1 -oE -- "$pattern" "$profile")"
    else
        ui_result "$ui_log" FAIL "Effects: $what" "no line matching $pattern reached the profile"
    fi
}

built_after_last_switch() {
    local log="$1" tries=0 switched
    while true; do
        switched=$(grep -n 'switching to config: ' "$log" | tail -1 | cut -d: -f1)
        if [ -n "$switched" ] && awk -v after="$switched" 'NR > after && /creating effect 0: / { found = 1 } END { exit !found }' "$log"; then return 0; fi
        if [ "$tries" -ge 100 ]; then return 1; fi
        tries=$((tries + 1))
        sleep 0.1
    done
}

drive_effects() {
    local log="$1" home="$2" map="$3" ui_log="$1.ui" window configs="$2/.config/vkdaedalus/configs" profiles="$2/.config/vkdaedalus/profiles" first now x y tx ty y1 top bottom dragged added shown switches label unchosen reloads stalls waited frame
    local profile="$2/.config/vkdaedalus/profiles/vkcube.conf" settings="$2/.config/vkdaedalus/settings.conf"
    : >"$ui_log"
    if ! wait_for_line 'present cycle' 30 "$log"; then
        ui_result "$ui_log" FAIL "the Effects page" "the game never reached 30 presents"
        return 0
    fi
    window=$(DISPLAY="$PRIVATE_DISPLAY" xdotool search --onlyvisible --name '' | tail -1) || true
    ui_take_window "$window"
    if ! hold_key_until "$OVERLAY_KEY" 'overlay opened' 1 "$log" || ! ui_wait "$map" "smaa" "" "" "$log"; then
        ui_result "$ui_log" FAIL "the Effects page" "the overlay never opened on the effect list"
        return 0
    fi
    ui_click "$map" "...##actions" 1 "$log" || true
    ui_click "$map" "Move down" 1 "$log" || true
    if file_says "$profile" '^effects = smaa:cas:half_precision:every_param_type:pack_ui$'; then
        ui_click "$map" "...##actions" 1 "$log" || true
        ui_click "$map" "Move down" 1 "$log" || true
        if file_says "$profile" '^effects = cas:smaa:half_precision:every_param_type:pack_ui$'; then
            ui_result "$ui_log" PASS "Effects: a row's actions button" "its ... button moved cas below smaa and back, each saved"
        else
            ui_result "$ui_log" FAIL "Effects: a row's actions button" "moving smaa back down left $(grep -m1 '^effects' "$profile")"
        fi
    else
        ui_result "$ui_log" FAIL "Effects: a row's actions button" "its ... button and Move down left $(grep -m1 '^effects' "$profile")"
    fi
    ui_state "$map" fx-01-rows-moved "$ui_log"
    ui_click "$map" "Effects###effectsswitch" 1 "$log" || true
    if wait_for_count 'effects off' 1 "$log"; then ui_state "$map" fx-02-effects-off "$ui_log" "the effects switch clicked off"; fi
    if wait_for_count 'effects off' 1 "$log" && ui_click "$map" "Effects###effectsswitch" 1 "$log" && wait_for_count 'effects on' 1 "$log"; then
        ui_result "$ui_log" PASS "Effects: the effects switch" "two clicks turned the effects off and on"
    else
        ui_result "$ui_log" FAIL "Effects: the effects switch" "the clicks never turned the effects off and back on"
    fi
    ui_click "$map" "##enabled" 1 "$log" || true
    hold_key_until "$OVERLAY_KEY" 'overlay closed' 1 "$log" || true
    local closedSaved=0
    if file_says "$profile" '^disabledEffects = cas$'; then closedSaved=1; fi
    hold_key_until "$OVERLAY_KEY" 'overlay opened' 1 "$log" || true
    if [ "$closedSaved" = 1 ] && ui_click "$map" "##enabled" 1 "$log" && wait_until_gone "$profile" '^disabledEffects = cas$'; then
        ui_result "$ui_log" PASS "Effects: a row's checkbox" "unticking cas, with the overlay closed at once, saved disabledEffects = cas; ticking it again took it out"
    else
        ui_result "$ui_log" FAIL "Effects: a row's checkbox" "unticking cas and closing the overlay at once never saved disabledEffects = cas, or ticking it again never took it out"
    fi
    ui_click "$map" "cas" 1 "$log" || true
    ui_set_value "$map" "Sharpness" 0.7 "$log" || true
    if ! file_says "$profile" 'casSharpness = 0\.7'; then
        ui_result "$ui_log" FAIL "Effects: a parameter slider" "0.7 typed into cas's Sharpness never reached the profile"
    else
        ui_result "$ui_log" PASS "Effects: a parameter slider" "0.7 typed into cas's Sharpness reached the profile"
        ui_state "$map" fx-03-parameter-set "$ui_log"
        if ui_wait "$map" "Sharpness default mark" 8 1 "$log" && ui_click "$map" "Sharpness default mark" 1 "$log" && wait_until_gone "$profile" 'casSharpness = 0\.7'; then
            ui_result "$ui_log" PASS "Effects: a parameter's default mark" "cas's Sharpness at 0.7 filled its mark, and a click on it set the default back"
        else
            ui_result "$ui_log" FAIL "Effects: a parameter's default mark" "cas's Sharpness at 0.7 never filled its mark, or a click on it left 0.7 in the profile"
        fi
        ui_set_value "$map" "Sharpness" 0.7 "$log" || true
        if ! file_says "$profile" 'casSharpness = 0\.7'; then
            ui_result "$ui_log" FAIL "Effects: Reset to defaults" "0.7 typed into cas's Sharpness again never reached the profile, so there was nothing to reset"
        else
            ui_click "$map" "cas" 3 "$log" || true
            ui_click "$map" "Reset to defaults" 1 "$log" || true
            if wait_until_gone "$profile" 'casSharpness = 0\.7'; then
                ui_result "$ui_log" PASS "Effects: Reset to defaults" "it took cas's Sharpness of 0.7 out of the profile"
            else
                ui_result "$ui_log" FAIL "Effects: Reset to defaults" "cas's Sharpness of 0.7 stayed in the profile"
            fi
        fi
        ui_state "$map" fx-04-reset "$ui_log"
    fi
    ui_click "$map" "half_precision" 1 "$log" || true
    ui_click "$map" "Half precision" 1 "$log" || true
    if file_says "$profile" '^half_precision\.halfPrecision = true$'; then
        ui_result "$ui_log" PASS "Effects: Half precision" "ticking it saved half_precision.halfPrecision = true"
    else
        ui_result "$ui_log" FAIL "Effects: Half precision" "ticking it never saved half_precision.halfPrecision = true"
    fi
    ui_state "$map" fx-05-half-precision "$ui_log"
    ui_click "$map" "every_param_type" 1 "$log" || true
    if ! ui_wait "$map" "P Bool" "" "" "$log"; then
        ui_result "$ui_log" FAIL "Effects: the parameter editors" "every_param_type never opened to show its parameters"
    else
        ui_click "$map" "P Bool" 1 "$log" || true
        param_reaches_profile "$profile" 'PBool = (true|1)$' "the bool parameter's checkbox" "$ui_log"
        ui_set_value "$map" "P Int" 7 "$log" || true
        param_reaches_profile "$profile" 'PInt = 7$' "the int parameter's slider" "$ui_log"
        ui_click "$map" "P Combo" 1 "$log" || true
        ui_click "$map" "Two" 1 "$log" || true
        param_reaches_profile "$profile" 'PCombo = 2$' "the int parameter's combo" "$ui_log"
        ui_set_value "$map" "P Uint" 7 "$log" || true
        param_reaches_profile "$profile" 'PUint = 7$' "the uint parameter's slider" "$ui_log"
        ui_set_value "$map" "P Float2" 0.25 "$log" || true
        param_reaches_profile "$profile" 'PFloat2\[[01]\] = 0\.25' "the float2 parameter's slider" "$ui_log"
        ui_set_value "$map" "P Int2" 7 "$log" || true
        param_reaches_profile "$profile" 'PInt2\[[01]\] = 7$' "the int2 parameter's slider" "$ui_log"
        ui_set_value "$map" "P Uint2" 7 "$log" || true
        param_reaches_profile "$profile" 'PUint2\[[01]\] = 7$' "the uint2 parameter's slider" "$ui_log"
        ui_state "$map" fx-06-every-parameter-type "$ui_log"
        ui_click "$map" "every_param_type" 1 "$log" || true
    fi
    ui_click "$map" "pack_ui" 1 "$log" || true
    if ! ui_wait "$map" "P Radius" "" "" "$log"; then
        ui_result "$ui_log" FAIL "Effects: a pack's annotations" "pack_ui never opened to show its parameters"
    else
        shown=""
        if [ -n "$(ui_line "$map" "P Hidden")" ]; then shown="$shown, P Hidden though it says hidden = true"; fi
        if [ -n "$(ui_line "$map" "P Advanced")" ]; then shown="$shown, P Advanced though its category starts closed"; fi
        if [ -n "$(ui_line "$map" " ")" ]; then shown="$shown, a row for the author's text"; fi
        if [ -z "$(ui_line "$map" "pack_ui info")" ]; then shown="$shown, no i for the author's text on pack_ui's row"; fi
        if [ -n "$(ui_line "$map" "every_param_type info")" ]; then shown="$shown, an i on every_param_type's row, which has no author text"; fi
        ui_click "$map" "Advanced" 1 "$log" || true
        if ! ui_wait "$map" "P Advanced" "" "" "$log"; then shown="$shown, P Advanced still missing once its category was opened"; fi
        if [ -z "$shown" ]; then
            ui_result "$ui_log" PASS "Effects: a pack's annotations" "pack_ui's hidden parameter and author text took no row, its text sits behind an i, and its closed category opened on a click"
        else
            ui_result "$ui_log" FAIL "Effects: a pack's annotations" "pack_ui showed${shown#,}"
        fi
        ui_click "$map" "Finest" 1 "$log" || true
        param_reaches_profile "$profile" 'PQuality = 2$' "a radio parameter's choices" "$ui_log"
        ui_set_value "$map" "P Radius" 4 "$log" || true
        param_reaches_profile "$profile" 'PRadius = 4$' "a parameter with units" "$ui_log"
        ui_state "$map" fx-06b-pack-annotations "$ui_log" "pack_ui: a category, a closed one opened, a radio as choices, units and a colour"
        ui_click "$map" "P Colour" 1 "$log" || true
        if ui_wait "$map" "##Text" "" "" "$log" && ui_click "$map" "##Text" 1 "$log"; then
            ui_key End || true
            for i in 1 2 3 4 5 6 7 8; do ui_key BackSpace || true; done
            ui_type "d9a441" || true
            ui_key Return || true
        fi
        param_reaches_profile "$profile" 'PColour\[0\] = 0\.85' "a colour parameter's picker" "$ui_log"
        ui_click "$map" "pack_ui" 1 "$log" || true
        if ! ui_gone "$map" "P Radius" "$log"; then ui_click "$map" "pack_ui" 1 "$log" || true; fi
        if ! ui_gone "$map" "P Radius" "$log"; then ui_result "$ui_log" FAIL "Effects: a pack's annotations" "pack_ui stayed open after the picker closed and its row was clicked"; fi
    fi
    ui_click "$map" "smaa" 3 "$log" || true
    ui_click "$map" "Insert effects here..." 1 "$log" || true
    ui_click "$map" "dls" 1 "$log" || true
    ui_state "$map" fx-07-insert-here "$ui_log" "Insert effects here, from smaa's menu, with dls chosen"
    ui_click "$map" "Add 1 effect###addchosen" 1 "$log" || true
    if wait_for_line 'creating effect 1: dls' 1 "$log"; then
        ui_result "$ui_log" PASS "Effects: Insert effects here" "dls, chosen from smaa's menu, was built at smaa's place, index 1"
    else
        ui_result "$ui_log" FAIL "Effects: Insert effects here" "dls chosen from smaa's menu was never built at index 1"
    fi
    ui_state "$map" fx-08-inserted "$ui_log"
    ui_click "$map" "Add effects..." 1 "$log" || true
    ui_click "$map" "rcas" 1 "$log" || true
    ui_click "$map" "fxaa" 1 "$log" || true
    ui_wait "$map" "Add 2 effects###addchosen" "" "" "$log" || true
    ui_state "$map" fx-09-two-chosen "$ui_log" "Add effects with rcas and fxaa chosen"
    if ui_wait "$map" "Add 2 effects###addchosen" "" "" "$log" && ui_click "$map" "x##fxaa" 1 "$log" && ui_wait "$map" "Add 1 effect###addchosen" "" "" "$log"; then
        ui_result "$ui_log" PASS "Add effects: a chosen effect's x" "the x took fxaa back out, leaving one to add"
    else
        ui_result "$ui_log" FAIL "Add effects: a chosen effect's x" "the x never took fxaa back out"
    fi
    ui_click "$map" "##search" 1 "$log" || true
    ui_type zzz || true
    if ui_gone "$map" "rcas" "$log"; then
        ui_result "$ui_log" PASS "Add effects: a search that matches nothing" "zzz typed into the search listed no effect"
    else
        ui_result "$ui_log" FAIL "Add effects: a search that matches nothing" "zzz typed into the search still listed rcas"
    fi
    ui_state "$map" fx-10-no-match "$ui_log"
    for _ in 1 2 3; do ui_key BackSpace || true; done
    ui_click "$map" "Cancel" 1 "$log" || true
    if ui_gone "$map" "##search" "$log"; then
        ui_result "$ui_log" PASS "Add effects: Cancel" "it closed the list and added nothing"
    else
        ui_result "$ui_log" FAIL "Add effects: Cancel" "the list stayed open"
    fi
    ui_state "$map" fx-11-add-cancelled "$ui_log"
    ui_click "$map" "Settings" 1 "$log" || true
    ui_set_value "$map" "##maxEffects" 5 "$log" || true
    ui_click "$map" "Effects" 1 "$log" || true
    ui_click "$map" "Add effects..." 1 "$log" || true
    if ui_wait "$map" "rcas" 7 1 "$log"; then
        ui_result "$ui_log" PASS "Add effects: greyed at the limit" "with five effects and a limit of five, rcas could not be added"
    else
        ui_result "$ui_log" FAIL "Add effects: greyed at the limit" "with five effects and a limit of five, rcas was still offered"
    fi
    ui_state "$map" fx-12-limit-greyed "$ui_log"
    ui_click "$map" "Cancel" 1 "$log" || true
    ui_click "$map" "Settings" 1 "$log" || true
    ui_set_value "$map" "##maxEffects" 20 "$log" || true
    ui_click "$map" "Effects" 1 "$log" || true
    first=$(sed -n 's/^effects = \([^:]*\).*/\1/p' "$profile")
    ui_wait "$map" "smaa" "" "" "$log" || true
    dragged=0
    read -r x y y1 top bottom <<<"$(ui_point "$map" "smaa")"
    DISPLAY="$PRIVATE_DISPLAY" xdotool mousemove --window "$window" "${x:-0}" "$(((${top:-0} + ${bottom:-0}) / 2))" click --repeat 10 4 || true
    sleep 0.5
    read -r x y y1 top bottom <<<"$(ui_point "$map" "smaa")"
    read -r tx ty _ _ _ <<<"$(ui_point "$map" "cas")"
    if [ -z "$y" ] || [ -z "$ty" ] || [ "$ty" -lt "${top:-0}" ] || [ "$y1" -gt "${bottom:-0}" ]; then
        ui_result "$ui_log" FAIL "Effects: drag to reorder" "smaa and cas were not both in the list's view to drag, after scrolling it to the top"
    else
        dragged=1 y=$(((y + y1) / 2)) ty=$((ty + 2))
        DISPLAY="$PRIVATE_DISPLAY" xdotool mousemove --window "$window" "$x" "$y" mousedown 1 || true
        sleep 0.3
        DISPLAY="$PRIVATE_DISPLAY" xdotool mousemove --window "$window" "$x" "$(((y + ty) / 2))" || true
        sleep 0.3
        DISPLAY="$PRIVATE_DISPLAY" xdotool mousemove --window "$window" "$tx" "$ty" || true
        sleep 0.3
        DISPLAY="$PRIVATE_DISPLAY" xdotool mouseup 1 || true
    fi
    if [ "$dragged" = 0 ]; then
        :
    elif wait_until_gone "$profile" "^effects = $first:"; then
        now=$(sed -n 's/^effects = //p' "$profile")
        ui_result "$ui_log" PASS "Effects: drag to reorder" "dragging smaa onto $first saved the order $now"
    else
        ui_result "$ui_log" FAIL "Effects: drag to reorder" "dragging smaa onto $first left $first first in the profile"
    fi
    ui_state "$map" fx-13-dragged "$ui_log"
    ui_click "$map" "Add effects..." 1 "$log" || true
    ui_click "$map" "##search" 1 "$log" || true
    ui_type slow || true
    unchosen=""
    for label in slow_compile slow_compile_b; do
        ui_click "$map" "$label" 1 "$log" || true
        ui_wait "$map" "x##$label" "" "" "$log" || unchosen="$unchosen $label"
    done
    added=$(date +%s%N)
    if [ -n "$unchosen" ]; then
        ui_result "$ui_log" FAIL "Effects: Cancel while compiling" "a click on$unchosen never put it under Adding"
        ui_click "$map" "Cancel" 1 "$log" || true
        ui_gone "$map" "##search" "$log" || true
    elif ! ui_click "$map" "Add 2 effects###addchosen" 1 "$log" || ! ui_wait "$map" "Cancel##compiling" "" "" "$log"; then
        ui_result "$ui_log" FAIL "Effects: Cancel while compiling" "two slow effects compiling never showed Cancel"
        ui_click "$map" "Cancel" 1 "$log" || true
    else
        shown=$((($(date +%s%N) - added) / 1000000))
        ui_state "$map" fx-14-compiling "$ui_log" "two slow effects compiling, with Cancel"
        ui_click "$map" "Cancel##compiling" 1 "$log" || true
        sleep 10
        if [ "$shown" -lt 900 ]; then
            ui_result "$ui_log" FAIL "Effects: Cancel while compiling" "Cancel showed $shown ms after Add, before the second it waits"
        elif [ "$(count 'creating effect [0-9]+: slow_compile' "$log")" -ne 0 ]; then
            ui_result "$ui_log" FAIL "Effects: Cancel while compiling" "after Cancel, $(count 'creating effect [0-9]+: slow_compile' "$log") slow effect(s) were built anyway"
        else
            ui_result "$ui_log" PASS "Effects: Cancel while compiling" "Cancel showed $shown ms into a slow compile and neither slow effect was built"
        fi
        ui_state "$map" fx-15-compile-cancelled "$ui_log"
    fi
    ui_click "$map" "Add effects..." 1 "$log" || true
    ui_click "$map" "##search" 1 "$log" || true
    ui_type slow || true
    ui_click "$map" "slow_compile" 1 "$log" || true
    if ! ui_wait "$map" "x##slow_compile" "" "" "$log"; then
        ui_result "$ui_log" FAIL "Effects: the game draws while an effect compiles" "a click on slow_compile never put it under Adding"
        ui_click "$map" "Cancel" 1 "$log" || true
    else
        reloads=$(count 'hot-reloading config and effects' "$log")
        ui_click "$map" "Add 1 effect###addchosen" 1 "$log" || true
        stalls="" waited=0 frame=$(ui_frame "$map")
        while [ "$(count 'hot-reloading config and effects' "$log")" -le "$reloads" ] && [ "$waited" -lt 120 ] && [ ! -f "$log.rc" ]; do
            sleep 0.5
            waited=$((waited + 1))
            now=$(ui_frame "$map")
            if [ $((now - frame)) -lt 5 ]; then stalls="$stalls $frame to $now;"; fi
            frame=$now
        done
        if [ "$(count 'hot-reloading config and effects' "$log")" -le "$reloads" ]; then
            ui_result "$ui_log" FAIL "Effects: the game draws while an effect compiles" "slow_compile, added, never reached the chain in 60 s"
        elif [ -n "$stalls" ]; then
            ui_result "$ui_log" FAIL "Effects: the game draws while an effect compiles" "the overlay drew fewer than 5 frames in a half second while slow_compile was added (frames${stalls%;})"
        else
            ui_result "$ui_log" PASS "Effects: the game draws while an effect compiles" "slow_compile took $((waited / 2)) s to reach the chain and the overlay drew at least 5 frames every half second of it"
        fi
    fi
    ui_click "$map" "Settings" 1 "$log" || true
    ui_click "$map" "Apply automatically" 1 "$log" || true
    ui_click "$map" "Effects" 1 "$log" || true
    ui_click "$map" "##profile" 1 "$log" || true
    ui_click "$map" "none (vkDaedalus off)" 1 "$log" || true
    if ! ui_wait "$map" "Turn off" "" "" "$log"; then
        ui_result "$ui_log" FAIL "Effects: none asks first" "picking none never asked before turning vkDaedalus off"
    elif file_says "$configs/.active_profiles" '^vkcube=none$'; then
        ui_result "$ui_log" FAIL "Effects: none asks first" "picking none set vkcube=none before the question was answered"
    else
        ui_state "$map" fx-16-none-profile "$ui_log" "the question before vkDaedalus turns off for vkcube, naming both ways back"
        if ui_click "$map" "Cancel##offprofile" 1 "$log" && ui_gone "$map" "Turn off" "$log" && ! file_says "$configs/.active_profiles" '^vkcube=none$'; then
            ui_result "$ui_log" PASS "Effects: none asks first" "picking none asked first, and Cancel left vkcube on its profile"
        else
            ui_result "$ui_log" FAIL "Effects: none asks first" "Cancel left the question open or turned vkDaedalus off"
        fi
    fi
    switches=$(count 'switching to config: .*/profiles/vkcube\.conf$' "$log")
    ui_click "$map" "##profile" 1 "$log" || true
    ui_click "$map" "none (vkDaedalus off)" 1 "$log" || true
    ui_click "$map" "Turn off" 1 "$log" || true
    if ! file_says "$configs/.active_profiles" '^vkcube=none$'; then
        ui_result "$ui_log" FAIL "Effects: none in this run" "Turn off never set vkcube=none"
    elif ! wait_for_line 'vkDaedalus is off in this run' 1 "$log" || ! wait_for_line 'the frame was presented and VK_ERROR_OUT_OF_DATE_KHR returned' 1 "$log" ||
        ! wait_for_line "the game's new swapchain stays its own" 1 "$log"; then
        ui_result "$ui_log" FAIL "Effects: none in this run" "off said $(count 'vkDaedalus is off in this run' "$log") times, a rebuild asked $(count 'VK_ERROR_OUT_OF_DATE_KHR returned' "$log"), a swapchain handed back $(count "the game's new swapchain stays its own" "$log")"
    else
        ui_result "$ui_log" PASS "Effects: none in this run" "Turn off set vkcube=none and stopped the effects, and the game rebuilt its swapchain after VK_ERROR_OUT_OF_DATE_KHR and kept it with its own images"
        frame=$(ui_frame "$map")
        hold_key_until "$OVERLAY_KEY" 'vkDaedalus is back in this run' 1 "$log" || true
        waited=0
        while [ "$(ui_frame "$map")" -le "$frame" ] && [ "$waited" -lt 100 ] && [ ! -f "$log.rc" ]; do
            sleep 0.1
            waited=$((waited + 1))
        done
        if [ "$(count 'vkDaedalus is back in this run' "$log")" -ne 1 ] || [ "$(count 'VK_ERROR_OUT_OF_DATE_KHR returned' "$log")" -ne 2 ]; then
            ui_result "$ui_log" FAIL "Effects: the overlay key brings vkDaedalus back" "back said $(count 'vkDaedalus is back in this run' "$log") times, rebuilds asked $(count 'VK_ERROR_OUT_OF_DATE_KHR returned' "$log"), want 1 and 2"
        elif [ "$(ui_frame "$map")" -le "$frame" ]; then
            ui_result "$ui_log" FAIL "Effects: the overlay key brings vkDaedalus back" "the overlay drew no frame after it came back, still at frame $frame"
        elif ! file_says "$configs/.active_profiles" '^vkcube=vkcube$'; then
            ui_result "$ui_log" FAIL "Effects: the overlay key brings vkDaedalus back" "it came back but configs/.active_profiles still says $(grep -m1 '^vkcube=' "$configs/.active_profiles" || echo nothing for vkcube)"
        else
            ui_result "$ui_log" PASS "Effects: the overlay key brings vkDaedalus back" "the overlay key in the same run took the rebuilt swapchain over again, opened the overlay and put vkcube back on its profile"
        fi
    fi
    if ! file_says "$settings" '^autoApply = false$'; then
        ui_result "$ui_log" FAIL "Effects: a profile picked with Apply automatically off" "Apply automatically never turned off in settings.conf"
    elif wait_for_count 'switching to config: .*/profiles/vkcube\.conf$' $((switches + 1)) "$log" && built_after_last_switch "$log"; then
        ui_result "$ui_log" PASS "Effects: a profile picked with Apply automatically off" "coming back to the vkcube profile built its chain without Apply"
    else
        ui_result "$ui_log" FAIL "Effects: a profile picked with Apply automatically off" "coming back to the vkcube profile showed its effects but never built them; the chain waited for Apply"
    fi
    ui_click "$map" "##profile" 1 "$log" || true
    ui_click "$map" "Rename vkcube...##renameprofile" 1 "$log" || true
    if ! ui_wait "$map" "##renameprofile" "" "" "$log"; then
        ui_result "$ui_log" FAIL "Effects: rename a profile two games run" "the profile menu's Rename never opened the rename popup"
    else
        ui_click "$map" "##renameprofile" 1 "$log" || true
        ui_key End || true
        for _ in 1 2 3 4 5 6; do ui_key BackSpace || true; done
        ui_type shared || true
        ui_state "$map" fx-19-rename-profile "$ui_log" "Rename with a new name typed, saying the two games that run it keep it"
        ui_click "$map" "Rename" 1 "$log" || true
        if ! wait_for_line 'switching to config: .*/profiles/shared\.conf$' 1 "$log"; then
            ui_result "$ui_log" FAIL "Effects: rename a profile two games run" "Rename never switched the layer to profiles/shared.conf"
        elif [ -e "$profiles/vkcube.conf" ] || ! file_says "$profiles/shared.conf" '^effects = ' || ! file_says "$configs/.active_profiles" '^vkcube=shared$' || ! file_says "$configs/.active_profiles" '^othergame=shared$'; then
            ui_result "$ui_log" FAIL "Effects: rename a profile two games run" "profiles/vkcube.conf left: $([ -e "$profiles/vkcube.conf" ] && echo yes || echo no); entries: $(tr '\n' ' ' <"$configs/.active_profiles")"
        else
            ui_click "$map" "##profile" 1 "$log" || true
            if ui_wait "$map" "shared (2 games)###profile shared" "" "" "$log"; then
                ui_result "$ui_log" PASS "Effects: rename a profile two games run" "vkcube's profile renamed to shared: its file moved, vkcube and othergame both run it, the menu says 2 games and the layer switched to it"
                ui_state "$map" fx-20-renamed "$ui_log" "the profile menu after the rename, shared run by both games"
            else
                ui_result "$ui_log" FAIL "Effects: rename a profile two games run" "the renamed profile was never listed as shared (2 games): $(ui_label_starting "$map" "shared (" "$log" || echo not listed)"
            fi
            ui_click "$map" "##profile" 1 "$log" || true
        fi
    fi
    ui_click "$map" "##profile" 1 "$log" || true
    ui_click "$map" "New...##newprofile" 1 "$log" || true
    if ui_wait "$map" "##newprofname" "" "" "$log" && ui_click "$map" "Cancel##newprofile" 1 "$log" && ui_gone "$map" "##newprofname" "$log"; then
        ui_result "$ui_log" PASS "Effects: New profile's Cancel" "it closed the popup"
    else
        ui_result "$ui_log" FAIL "Effects: New profile's Cancel" "the popup stayed open or never opened"
    fi
    ui_state "$map" fx-17-new-cancelled "$ui_log"
    ui_click "$map" "##profile" 1 "$log" || true
    ui_click "$map" "New...##newprofile" 1 "$log" || true
    ui_click "$map" "##newprofname" 1 "$log" || true
    ui_type temp || true
    ui_click "$map" "Create" 1 "$log" || true
    if ! wait_for_count 'switching to config: .*/profiles/temp\.conf' 1 "$log"; then
        ui_result "$ui_log" FAIL "Effects: Delete profile's Cancel" "Create with temp never switched to profiles/temp.conf, so there was no profile to delete"
        return 0
    fi
    ui_click "$map" "##profile" 1 "$log" || true
    ui_click "$map" "Delete temp...##delprofile" 1 "$log" || true
    if ! ui_wait "$map" "Yes, delete" "" "" "$log"; then
        ui_result "$ui_log" FAIL "Effects: Delete profile's Cancel" "the profile menu's Delete never opened the delete popup"
    elif ! ui_click "$map" "Cancel" 1 "$log" || ! ui_gone "$map" "Yes, delete" "$log"; then
        ui_result "$ui_log" FAIL "Effects: Delete profile's Cancel" "Cancel left the delete popup open"
    elif [ ! -e "$profiles/temp.conf" ]; then
        ui_result "$ui_log" FAIL "Effects: Delete profile's Cancel" "Cancel deleted profiles/temp.conf"
    else
        ui_result "$ui_log" PASS "Effects: Delete profile's Cancel" "it closed the popup and kept profiles/temp.conf"
    fi
    ui_state "$map" fx-18-delete-cancelled "$ui_log"
    ui_click "$map" "Add effects..." 1 "$log" || true
    if ! ui_wait "$map" "##search" "" "" "$log"; then
        ui_result "$ui_log" FAIL "keys typed while the overlay is hidden stay with the game" "Add effects... never showed the effect list"
    elif ! ui_wait "$map" "rcas" "" "" "$log"; then
        ui_result "$ui_log" FAIL "keys typed while the overlay is hidden stay with the game" "the effect list never listed rcas before the overlay closed, so the check could not start"
        ui_click "$map" "Cancel" 1 "$log" || true
    else
        ui_click "$map" "##search" 1 "$log" || true
        hold_key_until "$OVERLAY_KEY" 'overlay closed' 1 "$log" || true
        ui_type zzq || true
        hold_key_until "$OVERLAY_KEY" 'overlay opened' 1 "$log" || true
        if ui_wait "$map" "rcas" "" "" "$log"; then
            ui_result "$ui_log" PASS "keys typed while the overlay is hidden stay with the game" "zzq typed with the overlay closed never reached its search, rcas stayed listed"
        else
            ui_result "$ui_log" FAIL "keys typed while the overlay is hidden stay with the game" "zzq typed with the overlay closed reached the search when it opened again, and the list lost rcas"
        fi
        ui_click "$map" "Cancel" 1 "$log" || true
    fi
}

drive_gpus_tab() {
    local log="$1" map="$2" profile="$3" ui_log="$1.ui" window
    : >"$ui_log"
    if ! wait_for_line 'present cycle' 30 "$log"; then
        ui_result "$ui_log" FAIL "the GPUs page" "the game never reached 30 presents"
        return 0
    fi
    window=$(DISPLAY="$PRIVATE_DISPLAY" xdotool search --onlyvisible --name '' | tail -1) || true
    ui_take_window "$window"
    if ! hold_key_until "$OVERLAY_KEY" 'overlay opened' 1 "$log"; then
        ui_result "$ui_log" FAIL "the GPUs page" "$OVERLAY_KEY was held but the overlay never opened"
        return 0
    fi
    ui_click "$map" "GPUs" 1 "$log" || true
    if ! ui_wait "$map" "Down##2" "" "" "$log"; then
        ui_result "$ui_log" FAIL "the GPUs page" "two GPUs listed in the profile never showed with their buttons"
        return 0
    fi
    ui_state "$map" gpus-03-listed "$ui_log" "the GPUs card with two GPUs the profile lists"
    ui_click "$map" "Down##2" 1 "$log" || true
    if file_says "$profile" '^gpus = 9abc:def0,1234:5678$'; then
        ui_result "$ui_log" PASS "GPUs: Down" "GPU 2's Down put 9abc:def0 first in the profile"
    else
        ui_result "$ui_log" FAIL "GPUs: Down" "GPU 2's Down never swapped the two GPUs in the profile"
    fi
    ui_state "$map" gpus-03-moved-down "$ui_log"
    ui_click "$map" "Up##3" 1 "$log" || true
    if file_says "$profile" '^gpus = 1234:5678,9abc:def0$'; then
        ui_result "$ui_log" PASS "GPUs: Up" "GPU 3's Up put 1234:5678 first again"
    else
        ui_result "$ui_log" FAIL "GPUs: Up" "GPU 3's Up never swapped them back"
    fi
    ui_state "$map" gpus-04-moved-up "$ui_log"
    ui_click "$map" "Remove##3" 1 "$log" || true
    if file_says "$profile" '^gpus = 1234:5678$'; then
        ui_result "$ui_log" PASS "GPUs: Remove" "GPU 3's Remove left only 1234:5678 in the profile"
    else
        ui_result "$ui_log" FAIL "GPUs: Remove" "GPU 3's Remove never took 9abc:def0 out of the profile"
    fi
    ui_state "$map" gpus-05-removed "$ui_log"
}

drive_locked_checkbox() {
    local log="$1" map="$2" ui_log="$1.ui" window
    : >"$ui_log"
    if ! wait_for_line 'present cycle' 30 "$log"; then
        ui_result "$ui_log" FAIL "a locked Safe Anti-Cheat checkbox is greyed" "the game never reached 30 presents"
        return 0
    fi
    window=$(DISPLAY="$PRIVATE_DISPLAY" xdotool search --onlyvisible --name '' | tail -1) || true
    ui_take_window "$window"
    if ! hold_key_until "$OVERLAY_KEY" 'overlay opened' 1 "$log"; then
        ui_result "$ui_log" FAIL "a locked Safe Anti-Cheat checkbox is greyed" "$OVERLAY_KEY was held but the overlay never opened"
    elif ui_wait "$map" "Safe Anti-Cheat" 7 1 "$log" && [ "$(ui_field "$map" "Safe Anti-Cheat" 8)" = 1 ]; then
        ui_result "$ui_log" PASS "a locked Safe Anti-Cheat checkbox is greyed" "with EasyAntiCheat shipped, the checkbox shows ticked and cannot be clicked"
    else
        ui_result "$ui_log" FAIL "a locked Safe Anti-Cheat checkbox is greyed" "with EasyAntiCheat shipped, the checkbox was not greyed and ticked (disabled $(ui_field "$map" "Safe Anti-Cheat" 7), ticked $(ui_field "$map" "Safe Anti-Cheat" 8))"
    fi
    ui_state "$map" ac-01-locked "$ui_log"
}

write_lsfg_stand_in() {
    mkdir -p "$1/.local/share/vulkan/implicit_layer.d"
    printf '%s\n' '{ "file_format_version": "1.1.0", "layer": { "name": "VK_LAYER_LSFGVK_frame_generation", "description": "an lsfg-vk the loader lists and never loads", "implementation_version": "2", "library_path": "liblsfg-vk-layer-stand-in.so", "type": "GLOBAL", "api_version": "1.4.350", "disable_environment": { "VKDAEDALUS_LIVE_LSFGVK_STAND_IN": "1" } } }' >"$1/.local/share/vulkan/implicit_layer.d/VkLayer_LSFGVK_frame_generation.json"
}

real_lsfg_reads_case() {
    local dir="$1" written="$2" icd="$3" library output errors
    if [ -z "$lsfg_manifest" ]; then
        record SKIP "lsfg-vk reads the file the overlay wrote" "no lsfg-vk named: --lsfg-vk <its layer manifest> runs this"
        return 0
    fi
    if [ ! -s "$written" ]; then
        record FAIL "lsfg-vk reads the file the overlay wrote" "the Frame generation run left no file at $written"
        return 0
    fi
    mkdir -p "$dir/real-lsfg"
    cp "$lsfg_manifest" "$dir/real-lsfg/"
    library=$(sed -n 's/.*"library_path": *"\(.*\)".*/\1/p' "$lsfg_manifest")
    output=$(env -u DISABLE_LSFGVK VK_DRIVER_FILES="$icd" VK_IMPLICIT_LAYER_PATH="$dir/real-lsfg" LSFGVK_CONFIG="$written" VK_LOADER_DEBUG=layer \
        vulkaninfo --summary 2>&1)
    errors=$(grep -E "^\(lsfg-vk\) \[ERROR\]: - .*configuration" <<<"$output" || true)
    if [ -z "$library" ] || ! grep -qF "Loading layer library $library" <<<"$output"; then
        record FAIL "lsfg-vk reads the file the overlay wrote" "the loader never loaded lsfg-vk from $lsfg_manifest (library ${library:-not named})"
    elif [ -n "$errors" ]; then
        record FAIL "lsfg-vk reads the file the overlay wrote" "$library refused it: $errors"
    else
        record PASS "lsfg-vk reads the file the overlay wrote" "$library read $written with no configuration error"
    fi
}

frame_order_case() {
    local place="$1/frame-order" after before named shell dynamic preloaded
    if [ ! -x "$VKB_LAYER/bin/vkdaedalus-run" ]; then
        record SKIP "vkdaedalus-run sets the order" "this build has no vkdaedalus-run (a plain meson build installs none)"
        return 0
    fi
    mkdir -p "$place/.config/vkdaedalus/configs"
    printf 'effectsAfterFrameGeneration = true\n' >"$place/.config/vkdaedalus/configs/After.exe.conf"
    local moved="$place/.config/vkdaedalus/profiles/After.exe.conf"
    after=$(env -u VKDAEDALUS_AFTER_LSFGVK HOME="$place" XDG_CONFIG_HOME="$place/.config" "$VKB_LAYER/bin/vkdaedalus-run" printenv VKDAEDALUS_AFTER_LSFGVK After.exe) || true
    before=$(env VKDAEDALUS_AFTER_LSFGVK=1 HOME="$place" XDG_CONFIG_HOME="$place/.config" "$VKB_LAYER/bin/vkdaedalus-run" printenv VKDAEDALUS_AFTER_LSFGVK Before.exe) || true
    if [ "$after" = 1 ] && [ -z "$before" ] && file_says "$moved" '^effectsAfterFrameGeneration = true$'; then
        record PASS "vkdaedalus-run sets the order" "a game whose own file from before shared profiles says effectsAfterFrameGeneration = true got VKDAEDALUS_AFTER_LSFGVK=1 once the launcher moved it to profiles/, a game without one did not, though it was started with it set"
    elif [ ! -f "$moved" ]; then
        record FAIL "vkdaedalus-run sets the order" "the launcher never moved After.exe.conf to profiles/, so it read the order before the layer's own move"
    else
        record FAIL "vkdaedalus-run sets the order" "with the profile saying true it set '${after}', without a profile '${before}'; want 1 and nothing"
    fi
    named=$(HOME="$place" XDG_CONFIG_HOME="$place/.config" "$VKB_LAYER/bin/vkdaedalus-run" printenv VKDAEDALUS_RUN_PROGRAM VKDAEDALUS_RUN_EXECUTABLE "$place/games/Named/Named.exe" | tr '\n' ' ') || true
    if [ "$named" = "Named.exe $place/games/Named/Named.exe " ]; then
        record PASS "vkdaedalus-run names the game" "it exported Named.exe and its path for the layer to keep to"
    else
        record FAIL "vkdaedalus-run names the game" "it exported '${named}', want Named.exe and $place/games/Named/Named.exe"
    fi
    local orders="$VKB_LAYER/share/vkdaedalus/order-layers" fresh inherited
    fresh=$(env -u VK_ADD_IMPLICIT_LAYER_PATH HOME="$place" XDG_CONFIG_HOME="$place/.config" "$VKB_LAYER/bin/vkdaedalus-run" printenv VK_ADD_IMPLICIT_LAYER_PATH) || true
    inherited=$(env VK_ADD_IMPLICIT_LAYER_PATH=/elsewhere HOME="$place" XDG_CONFIG_HOME="$place/.config" "$VKB_LAYER/bin/vkdaedalus-run" printenv VK_ADD_IMPLICIT_LAYER_PATH) || true
    if [ "$fresh" = "$orders" ] && [ "$inherited" = "$orders:/elsewhere" ] && [ -f "$orders/vkdaedalus_before_lsfgvk.json" ] && [ -f "$orders/lsfgvk_before_vkdaedalus.json" ]; then
        record PASS "vkdaedalus-run reads its order layers first" "it put $orders, holding both order layers, before any implicit layer folder, ahead of one the game was started with"
    else
        record FAIL "vkdaedalus-run reads its order layers first" "it exported '${fresh}', and '${inherited}' with /elsewhere set; want $orders first, holding vkdaedalus_before_lsfgvk.json and lsfgvk_before_vkdaedalus.json"
    fi
    shell=$(sed -n '1s/^#!//p' "$VKB_LAYER/bin/vkdaedalus-run")
    dynamic=0
    if [ ! -f "$shell" ]; then
        dynamic=1
    elif grep -qa 'ld-linux' "$shell"; then
        dynamic=1
    fi
    if [[ "$VKB_LAYER" != /nix/store/* ]] && [ "$dynamic" = 1 ]; then
        record SKIP "vkdaedalus-run starts under a preload it cannot load" "this build outside the Nix store runs vkdaedalus-run on $shell, a dynamic shell; the Nix package ships a static one that survives such a preload"
    else
        preloaded=$(env LD_PRELOAD="$VKB_FAULT/lib/libvkdaedalus-test-needs-absent.so" HOME="$place" XDG_CONFIG_HOME="$place/.config" \
            "$VKB_LAYER/bin/vkdaedalus-run" "$shell" -c "printf %s \"\$ENABLE_VKDAEDALUS\"" 2>"$place/preloaded.err") || true
        if [ "$preloaded" = 1 ]; then
            record PASS "vkdaedalus-run starts under a preload it cannot load" "with a preloaded library whose own library is missing, as Steam's 64-bit overlay is outside its runtime, it set ENABLE_VKDAEDALUS=1 and started the game"
        else
            record FAIL "vkdaedalus-run starts under a preload it cannot load" "it printed '${preloaded}': $(head -1 "$place/preloaded.err")"
        fi
    fi
    local conf="$place/.config/lsfg-vk/conf.toml"
    write_lsfg_stand_in "$place"
    env -u LSFGVK_CONFIG -u LSFGVK_ENV -u DISABLE_LSFGVK HOME="$place" XDG_CONFIG_HOME="$place/.config" XDG_DATA_HOME="$place/.local/share" \
        LSFGVK_DLL_PATH="$place/missing/lsfg-vk.dll" VKDAEDALUS_LIVE_LSFGVK_STAND_IN=1 "$VKB_LAYER/bin/vkdaedalus-run" true Missing.exe || true
    : >"$place/lsfg-vk.dll"
    env -u LSFGVK_CONFIG -u LSFGVK_ENV -u DISABLE_LSFGVK HOME="$place" XDG_CONFIG_HOME="$place/.config" XDG_DATA_HOME="$place/.local/share" \
        LSFGVK_DLL_PATH="$place/lsfg-vk.dll" VKDAEDALUS_LIVE_LSFGVK_STAND_IN=1 "$VKB_LAYER/bin/vkdaedalus-run" true Prepared.exe || true
    if [ ! -f "$conf" ]; then
        record FAIL "vkdaedalus-run readies lsfg-vk" "after the two launches lsfg-vk had no file at $conf"
    elif grep -qx 'name = "vkdaedalus default"' "$conf" && grep -qx 'multiplier = 1' "$conf" && grep -qx 'active_in = "/Prepared.exe"' "$conf" &&
        ! grep -q 'Missing.exe' "$conf"; then
        record PASS "vkdaedalus-run readies lsfg-vk" "with lsfg-vk installed, a game with no lsfg-vk profile was listed at multiplier 1 in the lsfg-vk profile of default, the profile it runs, before it started, and with no lsfg-vk.dll none was added"
    else
        record FAIL "vkdaedalus-run readies lsfg-vk" "lsfg-vk's file after the two launches: $(tr '\n' ' ' <"$conf")"
    fi
    local active="$place/.config/vkdaedalus/configs/.active_profiles" shown refused
    printf 'Off.exe=none\n' >>"$active"
    env -u LSFGVK_CONFIG HOME="$place" XDG_CONFIG_HOME="$place/.config" "$VKB_LAYER/bin/vkdaedalus-run" --profile default true Off.exe || true
    if grep -qx 'Off.exe=none' "$active"; then
        record FAIL "vkdaedalus-run --profile brings a game back" "a game set to none still runs none after vkdaedalus-run --profile default: $(tr '\n' ' ' <"$active")"
    else
        record PASS "vkdaedalus-run --profile brings a game back" "a game set to none runs default from that start, so the layer loads again"
    fi
    shown=$(HOME="$place" XDG_CONFIG_HOME="$place/.config" "$VKB_LAYER/bin/vkdaedalus-profile" Named.exe none) || shown="exit $?"
    refused=$(HOME="$place" XDG_CONFIG_HOME="$place/.config" "$VKB_LAYER/bin/vkdaedalus-profile" Named.exe nosuch 2>&1) && refused="exit 0: $refused"
    if [ "$shown" = none ] && grep -qx 'Named.exe=none' "$active" && [[ "$refused" == *"there is no profile named nosuch"* ]] &&
        [ "$(HOME="$place" XDG_CONFIG_HOME="$place/.config" "$VKB_LAYER/bin/vkdaedalus-profile" Named.exe)" = none ]; then
        record PASS "vkdaedalus-profile sets a game's profile" "it set Named.exe to none and printed it, printed it again when asked, and refused a profile that does not exist with the list"
    else
        record FAIL "vkdaedalus-profile sets a game's profile" "setting none printed '${shown}', an unknown profile gave '${refused}', configs/.active_profiles: $(tr '\n' ' ' <"$active")"
    fi
}

drive_launch_notice() {
    local log="$1" map="$2" ui_log="$1.ui"
    : >"$ui_log"
    if ! ui_wait "$map" "Launch notice" "" "" "$log"; then
        ui_result "$ui_log" FAIL "the launch notice shows at a game's start" "the item map never listed the launch notice"
        return 0
    fi
    ui_state "$map" ui-00-launch-notice "$ui_log" "the launch notice at a game's start"
    if ! wait_for_line 'launch notice left' 1 "$log"; then
        ui_result "$ui_log" FAIL "the launch notice leaves by itself" "the layer never said the notice left"
    fi
}

drive_fixed_file() {
    local log="$1" map="$2" ui_log="$1.ui" window
    : >"$ui_log"
    if ! wait_for_line 'present cycle' 30 "$log"; then
        ui_result "$ui_log" FAIL "a fixed config file offers no profiles" "the game never reached 30 presents"
        return 0
    fi
    window=$(DISPLAY="$PRIVATE_DISPLAY" xdotool search --onlyvisible --name '' | tail -1) || true
    ui_take_window "$window"
    if ! hold_key_until "$OVERLAY_KEY" 'overlay opened' 1 "$log" || ! ui_wait "$map" "cas" "" "" "$log"; then
        ui_result "$ui_log" FAIL "a fixed config file offers no profiles" "the overlay never opened on the effect list"
        return 0
    fi
    if [ -n "$(ui_line "$map" "##profile")" ] || [ -n "$(ui_line "$map" "New...##newprofile")" ]; then
        ui_result "$ui_log" FAIL "a fixed config file offers no profiles" "with VKDAEDALUS_CONFIG_FILE set the profile menu or + was offered, though nothing chosen there is kept"
    else
        ui_result "$ui_log" PASS "a fixed config file offers no profiles" "with VKDAEDALUS_CONFIG_FILE set the Effects page showed cas with no profile menu, + or -"
    fi
    ui_state "$map" ui-13-fixed-file "$ui_log"
    ui_click "$map" "Safe Anti-Cheat" 1 "$log" || true
    if ! ui_wait "$map" "Safe Anti-Cheat" 8 0 "$log"; then
        ui_result "$ui_log" FAIL "a fixed config file: Safe Anti-Cheat runs and is not kept" "a click on the Safe Anti-Cheat tile never turned it off for this run"
    fi
    hold_key_until "$OVERLAY_KEY" 'overlay closed' 1 "$log" || true
}

drive_frame_generation() {
    local log="$1" map="$2" config="$3" ui_log="$1.ui" window switch='On###frameGeneration' x0 y
    : >"$ui_log"
    if ! wait_for_line 'present cycle' 30 "$log"; then
        ui_result "$ui_log" FAIL "frame generation is switched per game" "the game never reached 30 presents"
        return 0
    fi
    window=$(DISPLAY="$PRIVATE_DISPLAY" xdotool search --onlyvisible --name '' | tail -1) || true
    ui_take_window "$window"
    if ! hold_key_until "$OVERLAY_KEY" 'overlay opened' 1 "$log"; then
        ui_result "$ui_log" FAIL "frame generation is switched per game" "$OVERLAY_KEY was held but the overlay never opened"
        return 0
    fi
    ui_click "$map" "Frame generation" 1 "$log" || true
    ui_click "$map" "$switch" 1 "$log" || true
    sleep 1
    if cmp -s "$config" "$config.seed"; then
        ui_result "$ui_log" PASS "Frame generation: On without lsfg-vk.dll" "with LSFGVK_DLL_PATH naming a missing file, On left lsfg-vk's file as it was"
    else
        ui_result "$ui_log" FAIL "Frame generation: On without lsfg-vk.dll" "it changed lsfg-vk's file though lsfg-vk.dll was nowhere lsfg-vk looks"
    fi
    ui_state "$map" fg-02-dll-missing "$ui_log"
    : >"$(dirname "$config")/lsfg-vk.dll"
    if ! ui_click "$map" "$switch" 1 "$log" || ! file_says "$config" '^name = "vkdaedalus default"$' || ! file_says "$config" '^active_in = "/vkcube"$'; then
        ui_result "$ui_log" FAIL "frame generation is switched per game" "switching it on never wrote the lsfg-vk profile of default, the profile vkcube runs, listing vkcube into $config"
        return 0
    fi
    if ! grep -qx 'version = 2' "$config" || ! grep -qx 'multiplier = 2' "$config" || ! grep -qx 'color_space = "srgb"' "$config"; then
        ui_result "$ui_log" FAIL "frame generation is switched per game" "the profile written is not lsfg-vk 2 at multiplier 2 in the swapchain's sRGB: $(tr '\n' ' ' <"$config")"
        return 0
    fi
    ui_state "$map" fg-03-on "$ui_log" "On with lsfg-vk.dll found: vkcube's lsfg-vk profile written at 2x"
    ui_set_value "$map" "##Multiplier" 3 "$log" || true
    if file_says "$config" '^multiplier = 3$'; then
        ui_result "$ui_log" PASS "Frame generation: Multiplier" "3 typed into it reached the lsfg-vk profile"
    else
        ui_result "$ui_log" FAIL "Frame generation: Multiplier" "3 typed into it never reached the lsfg-vk profile"
    fi
    read -r x0 y <<<"$(awk -F'\t' 'NR > 1 && $2 == "##Multiplier" { printf "%d %d\n", $3, ($4 + $6) / 2; exit }' "$map")"
    DISPLAY="$PRIVATE_DISPLAY" xdotool mousemove --window "$window" "$((${x0:-0} + 150))" "${y:-0}" mousedown 1 || true
    sleep 0.3
    DISPLAY="$PRIVATE_DISPLAY" xdotool mousemove --window "$window" "$((${x0:-0} + 60))" "${y:-0}" || true
    sleep 0.3
    DISPLAY="$PRIVATE_DISPLAY" xdotool mousemove --window "$window" "$((${x0:-0} - 40))" "${y:-0}" || true
    sleep 0.3
    DISPLAY="$PRIVATE_DISPLAY" xdotool mouseup 1 || true
    if file_says "$config" '^multiplier = 2$'; then
        ui_result "$ui_log" PASS "Frame generation: Multiplier dragged" "dragging it from 3 past its left end saved multiplier = 2"
    else
        ui_result "$ui_log" FAIL "Frame generation: Multiplier dragged" "dragging it from 3 past its left end left $(grep -E '^multiplier' "$config")"
    fi
    ui_set_value "$map" "##Flow scale" 0.5 "$log" || true
    if file_says "$config" '^flow_scale = 0\.5$'; then
        ui_result "$ui_log" PASS "Frame generation: Flow scale" "0.5 typed into it reached the lsfg-vk profile"
    else
        ui_result "$ui_log" FAIL "Frame generation: Flow scale" "0.5 typed into it never reached the lsfg-vk profile"
    fi
    ui_set_value "$map" "##Multiplier" 99 "$log" || true
    ui_set_value "$map" "##Flow scale" 0.1 "$log" || true
    if file_says "$config" '^multiplier = 20$' && file_says "$config" '^flow_scale = 0\.25$'; then
        ui_result "$ui_log" PASS "Frame generation: typed values stay in range" "99 typed into Multiplier saved 20 and 0.1 typed into Flow scale saved 0.25"
    else
        ui_result "$ui_log" FAIL "Frame generation: typed values stay in range" "99 and 0.1 typed in left $(grep -E '^(multiplier|flow_scale)' "$config" | tr '\n' ' ')instead of 20 and 0.25"
    fi
    ui_state "$map" fg-04-values "$ui_log"
    ui_click "$map" "Performance mode" 1 "$log" || true
    if file_says "$config" '^performance_mode = true$'; then
        ui_result "$ui_log" PASS "Frame generation: Performance mode" "ticking it reached the lsfg-vk profile"
    else
        ui_result "$ui_log" FAIL "Frame generation: Performance mode" "ticking it never reached the lsfg-vk profile"
    fi
    ui_click "$map" "##Colour space" 1 "$log" || true
    ui_wait "$map" "HDR10" "" "" "$log" || true
    ui_state "$map" fg-05-colour-space-menu "$ui_log"
    ui_click "$map" "HDR10" 1 "$log" || true
    if file_says "$config" '^color_space = "hdr10"$'; then
        ui_result "$ui_log" PASS "Frame generation: Colour space" "HDR10 picked in it reached the lsfg-vk profile"
    else
        ui_result "$ui_log" FAIL "Frame generation: Colour space" "HDR10 picked in it never reached the lsfg-vk profile"
    fi
    ui_click "$map" "Effects after frame generation" 1 "$log" || true
    if file_says "$(dirname "$config")/.config/vkdaedalus/profiles/default.conf" '^effectsAfterFrameGeneration = true$'; then
        ui_result "$ui_log" PASS "Frame generation: effects after it" "ticking it saved effectsAfterFrameGeneration = true in default, the profile vkcube runs"
    else
        ui_result "$ui_log" FAIL "Frame generation: effects after it" "ticking it never saved effectsAfterFrameGeneration = true in default, the profile vkcube runs"
    fi
    restart_mark_says "$map" "Effects after frame generation" 1 "$log" "$ui_log" "Frame generation: effects after it waits for the next start" "ticked while this run runs effects before frame generation"
    ui_state "$map" fg-06-next-launch "$ui_log"
    ui_hover "$map" "Frame generation" "$log" || true
    ui_state "$map" fg-01-frame-generation "$ui_log" "the Frame generation card, its tile hovered"
    ui_click "$map" "$switch" 1 "$log" || true
    if ! file_says "$config" '^multiplier = 1$' || ! grep -qx 'name = "vkdaedalus default"' "$config"; then
        ui_result "$ui_log" FAIL "frame generation is switched per game" "switching it off did not keep default's lsfg-vk profile at multiplier 1: $(tr '\n' ' ' <"$config")"
        return 0
    fi
    if [ -n "$(diff --old-line-format='%L' --new-line-format='' --unchanged-line-format='' "$config.seed" "$config")" ]; then
        ui_result "$ui_log" FAIL "frame generation is switched per game" "the overlay removed or changed lines lsfg-vk's file held before it: $(diff --old-line-format='%L' --new-line-format='' --unchanged-line-format='' "$config.seed" "$config" | head -3 | tr '\n' ' ')"
        return 0
    fi
    ui_state "$map" fg-07-off "$ui_log" "switched off: vkcube's lsfg-vk profile kept at multiplier 1"
    hold_key_until "$OVERLAY_KEY" 'overlay closed' 1 "$log" || true
    if hold_key_until Insert 'frame generation key: ' 1 "$log" && file_says "$config" '^multiplier = 20$' && wait_for_count 'frame generation notice drew a frame' 1 "$log"; then
        ui_result "$ui_log" PASS "Frame generation: its key" "Insert, the overlay closed, put vkcube's profile back at 20x, its multiplier before Off, and the notice said $(grep -m1 'frame generation key: ' "$log" | sed -E 's/.*frame generation key: //')"
    else
        ui_result "$ui_log" FAIL "Frame generation: its key" "Insert left $(grep -E '^multiplier' "$config" | tr '\n' ' ')and the layer said: $(grep -m1 'frame generation key: ' "$log" || echo nothing)"
    fi
    if hold_key_until Insert 'frame generation key: ' 1 "$log" && file_says "$config" '^multiplier = 1$'; then
        ui_result "$ui_log" PASS "Frame generation: its key turns it off" "Insert again saved multiplier = 1"
    else
        ui_result "$ui_log" FAIL "Frame generation: its key turns it off" "Insert again left $(grep -E '^multiplier' "$config" | tr '\n' ' ')"
    fi
    if ! hold_key_until "$OVERLAY_KEY" 'overlay opened' 1 "$log" || ! ui_wait "$map" "##Half precision" "" "" "$log"; then
        ui_result "$ui_log" FAIL "Frame generation: Half precision waits for the next start" "the overlay never opened again on the Frame generation card"
    elif file_says "$config" '^allow_fp16 = true$' && ui_click "$map" "##Half precision" 1 "$log" && file_says "$config" '^allow_fp16 = false$'; then
        restart_mark_says "$map" "Half precision" 1 "$log" "$ui_log" "Frame generation: Half precision waits for the next start" "a click saved allow_fp16 = false"
    else
        ui_result "$ui_log" FAIL "Frame generation: Half precision waits for the next start" "a click left $(grep -m1 '^allow_fp16' "$config" || echo no allow_fp16), want allow_fp16 = false from the seeded true"
    fi
    ui_states_verdict "$ui_log"
    ui_result "$ui_log" PASS "frame generation is switched per game" "the overlay wrote vkcube's lsfg-vk profile (version 2, multiplier 2) on and removed it off"
}

drive_off_cost() {
    local log="$1" map="$2" window
    wait_for_line 'present cycle' 30 "$log" || return 0
    window=$(DISPLAY="$PRIVATE_DISPLAY" xdotool search --onlyvisible --name '' | tail -1) || true
    ui_take_window "$window"
    hold_key_until "$OVERLAY_KEY" 'overlay opened' 1 "$log" || return 0
    ui_click "$map" "##profile" 1 "$log" || true
    ui_click "$map" "none (vkDaedalus off)" 1 "$log" || true
    ui_click "$map" "Turn off" 1 "$log" || true
}

mean_present_interval() {
    awk -v from="$2" '/^fault_inject: present [0-9]+$/ { n++; if (n >= from) { if (last) { sum += $3 - last; gaps++ } last = $3 } }
        END { if (gaps) printf "%.4f", sum / gaps / 1e6 }' "$1"
}

present_mode_of() {
    local icd="$1" dir="$2"
    env -u DISPLAY -u WAYLAND_DISPLAY XDG_RUNTIME_DIR="$PRIVATE_RUNTIME" WAYLAND_DISPLAY="$PRIVATE_WAYLAND" DISPLAY="$PRIVATE_DISPLAY" VK_DRIVER_FILES="$icd" "${DEVICE_SELECT[@]}" vulkaninfo >"$dir/vulkaninfo.txt" 2>"$dir/vulkaninfo.err" || return 1
    if [ "$(count 'PRESENT_MODE_IMMEDIATE_KHR' "$dir/vulkaninfo.txt")" -ge 1 ]; then echo 0; else echo 2; fi
}

off_cost_case() {
    local icd="$1" dir="$2" name="per-frame cost of none in this run" log="$2/off-cost.log" control="$2/off-cost-control.log" home="$2/off-cost-home"
    local map="$2/off-cost.map" mode driver handedAt from with without why frames=60000
    if ! mode=$(present_mode_of "$icd" "$dir"); then
        record FAIL "$name" "vulkaninfo failed: $(tail -1 "$dir/vulkaninfo.err")"
        return
    fi
    [ "$mode" = 0 ] || frames=9000
    fresh_home "$home" cas
    printf 'overlayKey = %s\n' "$OVERLAY_KEY" >"$home/.config/vkdaedalus/settings.conf"
    background drive_off_cost "$log" "$map"
    driver=$!
    run "$log" "$home" "$icd" "$FAULT:$BASALT" 1 1 -- env VKDAEDALUS_UI_MAP="$map" FAULT_INJECT_COUNT_PRESENTS=each vkcube --c "$frames" --present_mode "$mode" --wsi xcb
    driver_finished "drive_off_cost"
    run "$control" "$home-control" "$icd" "$FAULT" 0 1 -- env FAULT_INJECT_COUNT_PRESENTS=each vkcube --c "$frames" --present_mode "$mode" --wsi xcb
    handedAt=$(grep -n "the game's new swapchain stays its own" "$log" | head -1 | cut -d: -f1)
    if ! why=$(exited_cleanly "$log"); then
        record FAIL "$name" "$why"
    elif [ -z "$handedAt" ]; then
        record FAIL "$name" "the overlay's Turn off never handed the game its swapchain back, so there is nothing to time"
    else
        from=$(awk -v at="$handedAt" 'NR < at && /^fault_inject: present [0-9]+$/ { n++ } END { print n + 200 }' "$log")
        with=$(mean_present_interval "$log" "$from")
        without=$(mean_present_interval "$control" "$from")
        if [ -z "$with" ] || [ -z "$without" ]; then
            record FAIL "$name" "fewer than 200 presents were left after the hand-back at present $((from - 200)) of $frames"
        else
            record MEAS "$name" "$with ms per frame from present $from on, after Turn off handed the swapchain back, against $without ms without the layer (present mode $mode, xcb)"
        fi
    fi
}

lane_ui() {
    local icd="$1" dir="$2" control="$2/overlay-control.log" log="$2/ui.log" home="$2/ui-home" map="$2/ui.map" driver why step name detail
    fresh_home "$home" cas:smaa
    mkdir -p "$home/.config/vkdaedalus/configs"
    printf 'overlayKey = %s\n' "$OVERLAY_KEY" >"$home/.config/vkdaedalus/settings.conf"
    printf '[Window][vkDaedalus]\nPos=50,50\nSize=400,500\nCollapsed=0\n\n' >"$home/.config/vkdaedalus/imgui.ini"
    printf 'shaderPath = %s\ntexturePath = %s\n' "$home-fx" "$home/.config/vkdaedalus/reshade/Textures" \
        >"$home/.config/vkdaedalus/shader_manager.conf"
    mkdir -p "$home/.config/vkdaedalus/profiles"
    printf 'effects = cas\nsafeAntiCheat = false\n' >"$home/.config/vkdaedalus/profiles/stale.conf"
    cp "$VKB_FIXTURES/effects/macro_gated_parameter.fx" "$VKB_FIXTURES/effects/texture_too_wide.fx" "$VKB_FIXTURES/must-fail/no_technique.fx" \
        "$VKB_FIXTURES/effects/depth_semantic_smoke.fx" "$home-fx/"
    printf 'effects = cas:smaa:macro_gated_parameter:texture_too_wide\nmacro_gated_parameter = %s\ntexture_too_wide = %s\n@RESHADE_DEPTH_INPUT_IS_REVERSED = 0\n' \
        "$home-fx/macro_gated_parameter.fx" "$home-fx/texture_too_wide.fx" >"$home/.config/vkdaedalus/profiles/safe.conf"
    background drive_ui "$log" "$home" "$map"
    driver=$!
    run "$log" "$home" "$icd" "$BASALT:$VALIDATION" 1 1 -- env VKDAEDALUS_UI_MAP="$map" vkcube --c "$UI_FRAMES" --wsi xcb --width 1280 --height 720
    driver_finished "drive_ui"
    if ! why=$(exited_cleanly "$log"); then
        record FAIL "overlay interactions" "$why"
    elif ! why=$(validation_verdict "$log" "$control"); then
        record FAIL "overlay interactions: validation" "$why"
    elif [ "$(count 'vkDaedalus err:' "$log")" -ne 0 ]; then
        record FAIL "overlay interactions" "$(grep -m1 'vkDaedalus err:' "$log")"
    elif [ "$(count 'with the layer lock held' "$log")" -ne 0 ]; then
        record FAIL "overlay interactions: compiles off the frame" "$(grep -m1 'with the layer lock held' "$log" | sed -E 's/^vkDaedalus warn: +//')"
    else
        record PASS "overlay interactions: compiles off the frame" "no compile ran under the layer lock across the profile switches, added effects and definition edits"
    fi
    if [ ! -s "$log.ui" ]; then
        record FAIL "overlay interactions" "the UI driver recorded nothing"
        return
    fi
    while IFS=$'\t' read -r step name detail; do
        record "$step" "overlay: $name" "$detail"
    done <"$log.ui"
    local fgLog="$dir/frame-generation.log" fgHome="$dir/frame-generation-home" fgMap="$dir/frame-generation.map" fgConfig="$dir/frame-generation-home/lsfg-vk.toml"
    fresh_home "$fgHome" cas
    printf 'overlayKey = %s\n' "$OVERLAY_KEY" >"$fgHome/.config/vkdaedalus/settings.conf"
    write_lsfg_stand_in "$fgHome"
    printf '# kept as it is\nversion = 2\n\n[global]\nallow_fp16 = true\n\n[[profile]]\nname = "another game"\nactive_in = "Other.exe"\nmultiplier = 3\n' >"$fgConfig.seed"
    cp "$fgConfig.seed" "$fgConfig"
    background drive_frame_generation "$fgLog" "$fgMap" "$fgConfig"
    driver=$!
    run "$fgLog" "$fgHome" "$icd" "$BASALT:$VALIDATION" 1 1 -- env VKDAEDALUS_UI_MAP="$fgMap" LSFGVK_CONFIG="$fgConfig" LSFGVK_DLL_PATH="$fgHome/lsfg-vk.dll" VKDAEDALUS_LIVE_LSFGVK_STAND_IN=1 vkcube --c "$UI_FRAMES" --wsi xcb --width 1280 --height 720
    driver_finished "drive_frame_generation"
    while IFS=$'\t' read -r step name detail; do
        record "$step" "$name" "$detail"
    done <"$fgLog.ui"
    frame_order_case "$dir"
    real_lsfg_reads_case "$dir" "$fgConfig" "$icd"
    if ! why=$(plain_verdict "$fgLog" "$control"); then
        record FAIL "frame generation is switched per game, cleanly" "$why"
    fi
    local fgNext="$dir/frame-generation-next.log"
    sed -i "s/^overlayKey = .*/overlayKey = $OVERLAY_KEY/" "$fgHome/.config/vkdaedalus/settings.conf"
    background drive_next_start_frame_generation "$fgNext" "$fgMap" "$fgConfig"
    driver=$!
    run "$fgNext" "$fgHome" "$icd" "$BASALT:$VALIDATION" 1 1 -- env VKDAEDALUS_UI_MAP="$fgMap" LSFGVK_CONFIG="$fgConfig" LSFGVK_DLL_PATH="$fgHome/lsfg-vk.dll" VKDAEDALUS_LIVE_LSFGVK_STAND_IN=1 vkcube --c "$UI_FRAMES" --wsi xcb --width 1280 --height 720
    driver_finished "drive_next_start_frame_generation"
    while IFS=$'\t' read -r step name detail; do
        record "$step" "$name" "$detail"
    done <"$fgNext.ui"
    if ! why=$(plain_verdict "$fgNext" "$control"); then
        record FAIL "frame generation's next start, cleanly" "$why"
    fi
    local ntLog="$dir/notice.log" ntHome="$dir/notice-home" ntMap="$dir/notice.map" drawn left
    fresh_home "$ntHome" cas
    background drive_launch_notice "$ntLog" "$ntMap"
    driver=$!
    run "$ntLog" "$ntHome" "$icd" "$BASALT:$VALIDATION" 1 1 -- env VKDAEDALUS_UI_MAP="$ntMap" VKDAEDALUS_LAUNCH_NOTICE=every vkcube --c 900 --wsi xcb --width 1280 --height 720
    driver_finished "drive_launch_notice"
    while IFS=$'\t' read -r step name detail; do
        record "$step" "$name" "$detail"
    done <"$ntLog.ui"
    drawn=$(count 'launch notice drew a frame' "$ntLog")
    left=$(awk '/launch notice left/ { gone = 1; next } gone && /launch notice drew a frame/ { after++ } END { print gone ? after + 0 : -1 }' "$ntLog")
    if ! why=$(plain_verdict "$ntLog" "$control"); then
        record FAIL "the launch notice shows at start and leaves" "$why"
    elif [ "$(count 'launch notice shown: Profile default, 1 effect on' "$ntLog")" -ne 1 ] || [ "$drawn" -lt 1 ] || [ "$left" -ne 0 ]; then
        record FAIL "the launch notice shows at start and leaves" "shown lines $(count 'launch notice shown' "$ntLog"), frames drawn $drawn, frames after it left $left (-1: it never left)"
    elif [ "$(count 'overlay recorded a frame|Input grabbed for overlay' "$ntLog")" -ne 0 ]; then
        record FAIL "the launch notice shows at start and leaves" "the notice opened the overlay or took the game's input"
    else
        record PASS "the launch notice shows at start and leaves" "it said Profile default, 1 effect on, drew $drawn frames with no input taken, then left and drew none"
    fi
    local fxLog="$dir/fixed-file.log" fxHome="$dir/fixed-file-home" fxMap="$dir/fixed-file.map"
    fresh_home "$fxHome" cas
    printf 'overlayKey = %s\n' "$OVERLAY_KEY" >"$fxHome/.config/vkdaedalus/settings.conf"
    cp "$fxHome/.config/vkdaedalus/vkdaedalus.conf" "$fxHome/fixed.conf"
    background drive_fixed_file "$fxLog" "$fxMap"
    driver=$!
    run "$fxLog" "$fxHome" "$icd" "$BASALT:$VALIDATION" 1 1 -- env VKDAEDALUS_UI_MAP="$fxMap" VKDAEDALUS_CONFIG_FILE="$fxHome/fixed.conf" VKDAEDALUS_LAUNCH_NOTICE=every vkcube --c 900 --wsi xcb --width 1280 --height 720
    driver_finished "drive_fixed_file"
    if grep -qs '^safeAntiCheat = false' "$fxHome/.config/vkdaedalus/configs/vkcube.conf"; then
        record FAIL "a fixed config file: Safe Anti-Cheat runs and is not kept" "the tile's click was saved in configs/vkcube.conf, though VKDAEDALUS_CONFIG_FILE decides at every launch"
    elif ! grep -q 'never turned it off for this run' "$fxLog.ui"; then
        record PASS "a fixed config file: Safe Anti-Cheat runs and is not kept" "the tile turned it off for this run and wrote nothing to configs/vkcube.conf"
    fi
    if [ "$(count 'launch notice shown: Config file fixed.conf, 1 effect on' "$fxLog")" -eq 1 ]; then
        record PASS "a fixed config file is named where the profile would be" "the launch notice said Config file fixed.conf"
    else
        record FAIL "a fixed config file is named where the profile would be" "the launch notice said: $(grep -m1 -o 'launch notice shown: .*' "$fxLog" || echo nothing)"
    fi
    while IFS=$'\t' read -r step name detail; do
        record "$step" "$name" "$detail"
    done <"$fxLog.ui"
    if ! why=$(plain_verdict "$fxLog" "$control"); then
        record FAIL "a fixed config file runs cleanly" "$why"
    fi
    local efLog="$dir/effects.log" efHome="$dir/effects-home" efMap="$dir/effects.map"
    fresh_home "$efHome" cas
    mkdir -p "$efHome/.config/vkdaedalus/configs" "$efHome/.config/vkdaedalus/profiles"
    cp "$VKB_FIXTURES/effects/half_precision.fx" "$VKB_FIXTURES/effects/every_param_type.fx" "$VKB_FIXTURES/effects/pack_ui.fx" "$efHome-fx/"
    mkdir -p "$efHome/.config/vkdaedalus/reshade/Shaders"
    cp "$VKB_FIXTURES/slow/slow_compile.fx" "$efHome/.config/vkdaedalus/reshade/Shaders/"
    cp "$VKB_FIXTURES/slow/slow_compile.fx" "$efHome/.config/vkdaedalus/reshade/Shaders/slow_compile_b.fx"
    printf 'overlayKey = %s\nautoApplyDelay = 1000\n' "$OVERLAY_KEY" >"$efHome/.config/vkdaedalus/settings.conf"
    printf 'effects = cas:smaa:half_precision:every_param_type:pack_ui\nhalf_precision = %s\nevery_param_type = %s\npack_ui = %s\n' \
        "$efHome-fx/half_precision.fx" "$efHome-fx/every_param_type.fx" "$efHome-fx/pack_ui.fx" \
        >"$efHome/.config/vkdaedalus/profiles/vkcube.conf"
    printf 'safeAntiCheat = true\n' >"$efHome/.config/vkdaedalus/configs/vkcube.conf"
    printf 'safeAntiCheat = true\n' >"$efHome/.config/vkdaedalus/configs/othergame.conf"
    printf 'vkcube=vkcube\nothergame=vkcube\n' >"$efHome/.config/vkdaedalus/configs/.active_profiles"
    background drive_effects "$efLog" "$efHome" "$efMap"
    driver=$!
    run "$efLog" "$efHome" "$icd" "$BASALT:$VALIDATION" 1 1 -- env VKDAEDALUS_UI_MAP="$efMap" vkcube --c "$((UI_FRAMES * 2))" --wsi xcb --width 1280 --height 720
    driver_finished "drive_effects"
    while IFS=$'\t' read -r step name detail; do
        record "$step" "$name" "$detail"
    done <"$efLog.ui"
    if ! why=$(plain_verdict "$efLog" "$control"); then
        record FAIL "the Effects page, cleanly" "$why"
    fi
    off_cost_case "$icd" "$dir"
    local guLog="$dir/gpus-tab.log" guHome="$dir/gpus-tab-home" guMap="$dir/gpus-tab.map"
    fresh_home "$guHome" cas
    mkdir -p "$guHome/.config/vkdaedalus/configs" "$guHome/.config/vkdaedalus/profiles"
    printf 'overlayKey = %s\n' "$OVERLAY_KEY" >"$guHome/.config/vkdaedalus/settings.conf"
    printf 'effects = cas\ngpus = 1234:5678,9abc:def0\n' >"$guHome/.config/vkdaedalus/profiles/default.conf"
    printf 'safeAntiCheat = true\n' >"$guHome/.config/vkdaedalus/configs/vkcube.conf"
    background drive_gpus_tab "$guLog" "$guMap" "$guHome/.config/vkdaedalus/profiles/default.conf"
    driver=$!
    run "$guLog" "$guHome" "$icd" "$BASALT:$VALIDATION" 1 1 -- env VKDAEDALUS_UI_MAP="$guMap" vkcube --c "$UI_FRAMES" --wsi xcb --width 1280 --height 720
    driver_finished "drive_gpus_tab"
    while IFS=$'\t' read -r step name detail; do
        record "$step" "$name" "$detail"
    done <"$guLog.ui"
    if ! why=$(plain_verdict "$guLog" "$control"); then
        record FAIL "the GPUs page, cleanly" "$why"
    fi
    local shLog="$dir/shaders.log" shHome="$dir/shaders-home" shMap="$dir/shaders.map" shPack="$dir/shaders-home-packs/pack"
    fresh_home "$shHome" cas
    mkdir -p "$shHome/.config/vkdaedalus/configs" "$shHome/.config/vkdaedalus/profiles" "$shPack/Shaders" "$shPack/Textures"
    cp "$VKB_FIXTURES/effects/solid_fill.fx" "$shPack/Shaders/"
    printf 'overlayKey = %s\n' "$OVERLAY_KEY" >"$shHome/.config/vkdaedalus/settings.conf"
    mkdir -p "$shHome-locked/pack"
    chmod 000 "$shHome-locked"
    printf 'parentDir = %s\nparentDir = %s\n' "$shPack" "$shHome-locked/pack" >"$shHome/.config/vkdaedalus/shader_manager.conf"
    cp "$VKB_FIXTURES/effects/global_definition.fx" "$shHome-fx/"
    printf 'effects = cas:global_definition\nglobal_definition = %s\n@RESHADE_DEPTH_INPUT_IS_REVERSED = 0\n' "$shHome-fx/global_definition.fx" >"$shHome/.config/vkdaedalus/profiles/default.conf"
    printf 'safeAntiCheat = true\n' >"$shHome/.config/vkdaedalus/configs/vkcube.conf"
    background drive_shaders "$shLog" "$shHome" "$shMap"
    driver=$!
    run "$shLog" "$shHome" "$icd" "$BASALT:$VALIDATION" 1 1 -- env VKDAEDALUS_UI_MAP="$shMap" vkcube --c "$((UI_FRAMES * 2))" --wsi xcb --width 1280 --height 720
    chmod 755 "$shHome-locked"
    driver_finished "drive_shaders"
    while IFS=$'\t' read -r step name detail; do
        record "$step" "$name" "$detail"
    done <"$shLog.ui"
    if ! why=$(plain_verdict "$shLog" "$control"); then
        record FAIL "the Shaders page, cleanly" "$why"
    fi
    local setLog="$dir/settings.log" setHome="$dir/settings-home" setMap="$dir/settings.map"
    fresh_home "$setHome" cas
    mkdir -p "$setHome/.config/vkdaedalus/configs" "$setHome/.config/vkdaedalus/profiles"
    printf 'overlayKey = %s\n' "$OVERLAY_KEY" >"$setHome/.config/vkdaedalus/settings.conf"
    cp "$VKB_FIXTURES/effects/macro_gated_parameter.fx" "$setHome-fx/"
    printf 'effects = cas:macro_gated_parameter\nmacro_gated_parameter = %s\n' "$setHome-fx/macro_gated_parameter.fx" \
        >"$setHome/.config/vkdaedalus/profiles/default.conf"
    printf 'safeAntiCheat = false\n' >"$setHome/.config/vkdaedalus/configs/vkcube.conf"
    background drive_settings "$setLog" "$setHome" "$setMap"
    driver=$!
    run "$setLog" "$setHome" "$icd" "$BASALT:$VALIDATION" 1 1 -- env VKDAEDALUS_UI_MAP="$setMap" vkcube --c "$((UI_FRAMES * 2))" --wsi xcb --width 1024 --height 768
    driver_finished "drive_settings"
    while IFS=$'\t' read -r step name detail; do
        record "$step" "$name" "$detail"
    done <"$setLog.ui"
    if ! why=$(plain_verdict "$setLog" "$control"); then
        record FAIL "the Settings page, cleanly" "$why"
    fi
    local setNext="$dir/settings-next.log" depthBefore depthAfter
    sed -i "s/^overlayKey = .*/overlayKey = $OVERLAY_KEY/" "$setHome/.config/vkdaedalus/settings.conf"
    background drive_next_start_settings "$setNext" "$setMap"
    driver=$!
    run "$setNext" "$setHome" "$icd" "$BASALT:$VALIDATION" 1 1 -- env VKDAEDALUS_UI_MAP="$setMap" vkcube --c "$UI_FRAMES" --wsi xcb --width 1024 --height 768
    driver_finished "drive_next_start_settings"
    while IFS=$'\t' read -r step name detail; do
        record "$step" "$name" "$detail"
    done <"$setNext.ui"
    depthBefore=$(count 'detected depth image' "$setLog")
    depthAfter=$(count 'detected depth image' "$setNext")
    if ! why=$(plain_verdict "$setNext" "$control"); then
        record FAIL "Settings: depth capture runs from the next start" "$why"
    elif [ "$depthBefore" -ne 0 ] || [ "$depthAfter" -lt 1 ]; then
        record FAIL "Settings: depth capture runs from the next start" "vkcube's depth image was seen $depthBefore times in the run that turned it on and $depthAfter times in the next; want 0, then at least 1: $(grep -m1 'Safe Anti-Cheat is on' "$setNext" || echo Safe Anti-Cheat said nothing)"
    else
        record PASS "Settings: depth capture runs from the next start" "the run that turned it on never tracked vkcube's depth image, the next start tracked it $depthAfter times"
    fi
    local szSpec szPercent szTag szDisplay szLog szHome szMap
    for szSpec in "84|1080p|1080p at 125% (84% at 1280x720)" "75|1440p|1440p at 150% (75% at 1280x720)"; do
        IFS='|' read -r szPercent szTag szDisplay <<<"$szSpec"
        szLog="$dir/sizes-$szTag.log" szHome="$dir/sizes-$szTag-home" szMap="$dir/sizes-$szTag.map"
        fresh_home "$szHome" cas:smaa
        printf 'overlayKey = %s\noverlaySize = %s\n' "$OVERLAY_KEY" "$szPercent" >"$szHome/.config/vkdaedalus/settings.conf"
        write_lsfg_stand_in "$szHome"
        printf '# kept as it is\nversion = 2\n\n[global]\nallow_fp16 = true\n\n[[profile]]\nname = "another game"\nactive_in = "Other.exe"\nmultiplier = 3\n' >"$szHome/lsfg-vk.toml"
        rm -f "$szMap"
        background drive_card_sizes "$szLog" "$szMap" "$szTag" "$szDisplay" "" Performance GPUs "Frame generation"
        driver=$!
        run "$szLog" "$szHome" "$icd" "$BASALT:$VALIDATION" 1 1 -- env VKDAEDALUS_UI_MAP="$szMap" LSFGVK_CONFIG="$szHome/lsfg-vk.toml" LSFGVK_DLL_PATH="$szHome/lsfg-vk.dll" VKDAEDALUS_LIVE_LSFGVK_STAND_IN=1 vkcube --c "$UI_FRAMES" --wsi xcb --width 1280 --height 720
        driver_finished "drive_card_sizes"
        while IFS=$'\t' read -r step name detail; do
            record "$step" "$name" "$detail"
        done <"$szLog.ui"
        if ! why=$(plain_verdict "$szLog" "$control"); then
            record FAIL "the cards at $szDisplay, cleanly" "$why"
        fi
    done
}

probe_ask() {
    local out="$1/probe.out" before tries=0
    before=$(count '' "$out")
    echo "$2" >&4
    while [ "$(count '' "$out")" -le "$before" ]; do
        tries=$((tries + 1))
        if [ "$tries" -gt 100 ]; then
            echo "no answer"
            return 0
        fi
        sleep 0.1
    done
    tail -1 "$out"
}

answer() {
    if [ -f "$1" ]; then cat "$1"; else echo "never asked"; fi
}

lane_focus() {
    local icd="$1" dir="$2" control="$2/overlay-control.log" log="$2/focus.log" home="$2/focus-home"
    local why presser driver probe before held freed tries=0
    : >"$dir/probe.out"
    mkfifo "$dir/probe.in"
    DISPLAY="$PRIVATE_DISPLAY" grab-probe <"$dir/probe.in" >"$dir/probe.out" &
    probe=$!
    exec 4>"$dir/probe.in"
    until [ "$(count '' "$dir/probe.out")" -ge 1 ] || [ "$tries" -ge 50 ]; do
        tries=$((tries + 1))
        sleep 0.1
    done
    if [ "$(sed -n 1p "$dir/probe.out")" != ready ]; then
        exec 4>&-
        if ! wait "$probe"; then :; fi
        record FAIL "focus loss releases input" "the grab probe did not start on $PRIVATE_DISPLAY: $(sed -n 1p "$dir/probe.out")"
        return
    fi
    before=$(probe_ask "$dir" grab)
    fresh_home "$home"
    printf 'overlayKey = %s\n' "$OVERLAY_KEY" >"$home/.config/vkdaedalus/settings.conf"
    background press_overlay_key "$log"
    presser=$!
    (
        if wait_for_line 'overlay opened' 1 "$log"; then
            sleep 0.5
            probe_ask "$dir" grab >"$dir/focus.held"
            probe_ask "$dir" focus >"$dir/focus.moved"
            if wait_for_line 'focus left the application' 1 "$log"; then sleep 0.5; fi
            probe_ask "$dir" grab >"$dir/focus.freed"
        fi
    ) &
    driver=$!
    run "$log" "$home" "$icd" "$BASALT:$VALIDATION" 1 1 -- vkcube --c "$FRAMES" --wsi xcb
    if ! wait "$presser"; then echo "the overlay key was never pressed" >"$dir/focus.held"; fi
    driver_finished "the focus watcher"
    exec 4>&-
    wait "$probe"
    held=$(answer "$dir/focus.held")
    freed=$(answer "$dir/focus.freed")
    if [ "$before" != free ]; then
        record FAIL "focus loss releases input" "the pointer was not free before the layer ran: $before"
    elif ! why=$(exited_cleanly "$log"); then
        record FAIL "focus loss releases input" "$why"
    elif ! why=$(validation_verdict "$log" "$control"); then
        record FAIL "focus loss: validation" "$why"
    elif [ "$held" != held ]; then
        record FAIL "focus loss releases input" "the open overlay held no pointer grab ($held), so moving focus proves nothing"
    elif [ "$(count 'focus left the application' "$log")" -lt 1 ]; then
        record FAIL "focus loss releases input" "focus moved to the root window ($(answer "$dir/focus.moved")) and the layer never noticed"
    elif [ "$freed" != free ]; then
        record FAIL "focus loss releases input" "the pointer grab outlived the focus ($freed), which strands the cursor"
    elif [ "$(count 'vkDaedalus err:' "$log")" -ne 0 ]; then
        record FAIL "focus loss releases input" "$(grep -m1 'vkDaedalus err:' "$log")"
    else
        record PASS "focus loss releases input" "the open overlay held the pointer; focus moved to the root window, the layer closed the overlay and the pointer came free"
    fi
    x11_hook_case "$icd" "$dir" "$control"
}

drive_x11_hook() {
    local log="$1" window
    wait_for_line 'x11-input-probe: holds the pointer' 1 "$log" || return 0
    window=$(DISPLAY="$PRIVATE_DISPLAY" xdotool search --onlyvisible --name 'x11-input-probe' | tail -1) || true
    ui_take_window "$window"
    hold_key_until "$OVERLAY_KEY" 'overlay opened' 1 "$log" || return 0
    sleep 0.5
    DISPLAY="$PRIVATE_DISPLAY" xdotool key a mousemove_relative 30 30 click 1 || true
    sleep 0.5
    hold_key_until "$OVERLAY_KEY" 'overlay closed' 1 "$log" || return 0
    sleep 0.5
    DISPLAY="$PRIVATE_DISPLAY" xdotool key b click 1 || true
}

x11_hook_case() {
    local icd="$1" dir="$2" control="$3" name="the input hook holds an X11 game's input" log="$2/x11-hook.log" home="$2/x11-hook-home" driver why opened closed during after
    if [ ! -x "$VKB_LAYER/bin/vkdaedalus-run" ]; then
        record SKIP "$name" "this build has no vkdaedalus-run (a plain meson build installs none)"
        return 0
    fi
    fresh_home "$home" cas
    printf 'overlayKey = %s\n' "$OVERLAY_KEY" >"$home/.config/vkdaedalus/settings.conf"
    background drive_x11_hook "$log"
    driver=$!
    run "$log" "$home" "$icd" "$BASALT:$VALIDATION" 1 1 -- "$VKB_LAYER/bin/vkdaedalus-run" x11-input-probe 900
    driver_finished "drive_x11_hook"
    opened=$(grep -n -m1 'overlay opened' "$log" | cut -d: -f1)
    closed=$(grep -n -m1 'overlay closed' "$log" | cut -d: -f1)
    if ! why=$(plain_verdict "$log" "$control"); then
        record FAIL "$name" "$why"
    elif ! file_says "$log" 'x11-input-probe: holds the pointer'; then
        record FAIL "$name" "the probe never held its own pointer grab, as Wine does, so the case proves nothing"
    elif [ -z "$opened" ] || [ -z "$closed" ]; then
        record FAIL "$name" "the overlay key never opened and closed the overlay over the probe"
    else
        during=$(awk -v a="$opened" -v b="$closed" 'NR > a && NR < b && /^x11-input-probe: (key press|button press|motion)/ { n++ } END { print n + 0 }' "$log")
        after=$(awk -v b="$closed" 'NR > b && /^x11-input-probe: key press/ { n++ } END { print n + 0 }' "$log")
        if [ "$during" -ne 0 ]; then
            record FAIL "$name" "the game got $during key, button or motion events while the overlay was open: $(awk -v a="$opened" -v b="$closed" 'NR > a && NR < b && /^x11-input-probe: (key press|button press|motion)/' "$log" | head -3 | tr '\n' ' ')"
        elif [ "$after" -lt 1 ]; then
            record FAIL "$name" "the game got no key once the overlay closed, so it may get no input at all"
        else
            record PASS "$name" "a game holding its own pointer grab, started with vkdaedalus-run, got no key, button or motion while the overlay was open and its keys again once it closed"
        fi
    fi
}

drive_controls() {
    local log="$1" home="$2" conf chain
    wait_for_line 'present cycle' 30 "$log" || return 0
    find "$home/.cache/vkdaedalus" -name '*.vkbfx' | wc -l >"$log.cache-start"
    hold_key_until "$TOGGLE_KEY" 'debug: effects off$' 1 "$log" || return 0
    hold_key_until "$TOGGLE_KEY" 'debug: effects on$' 1 "$log" || return 0
    hold_key_until "$RELOAD_KEY" 'reload key pressed' 1 "$log" || return 0
    wait_for_line 'effects reloaded successfully' 1 "$log" || return 0
    sleep 1.1
    conf=$(sed -n "s/.*hot-reload initialized, config: //p" "$log" | sed -n 1p)
    if [ -n "$conf" ]; then printf 'casSharpness = 0.6\n' >>"$conf"; fi
    wait_for_line 'effects reloaded successfully' 2 "$log" || return 0
    sleep 1.1
    printf 'safeAntiCheat = false\n' >"$home/.config/vkdaedalus/configs/vkcube.conf"
    wait_for_line 'effects reloaded successfully' 3 "$log" || return 0
    sleep 1.1
    chain=$(sed -n 's/^effects = //p' "$home/.config/vkdaedalus/vkdaedalus.conf")
    if [ -n "$conf" ]; then printf 'effects = %s:rcas\n' "$chain" >>"$conf"; fi
    wait_for_line 'effects reloaded successfully' 4 "$log" || return 0
    sleep 1.1
    if [ -n "$conf" ]; then : >"$conf"; fi
    wait_for_line 'effects reloaded successfully' 5 "$log" || return 0
}

lane_controls() {
    local icd="$1" dir="$2" control="$2/overlay-control.log" log="$2/controls.log" home="$2/controls-home"
    local why driver off on effects started ended
    fresh_home "$home"
    printf 'toggleKey = %s\nreloadKey = %s\n' "$TOGGLE_KEY" "${RELOAD_KEY,,}" >"$home/.config/vkdaedalus/settings.conf"
    background drive_controls "$log" "$home"
    driver=$!
    run "$log" "$home" "$icd" "$BASALT:$VALIDATION" 1 1 -- vkcube --c "$CONTROLS_FRAMES" --wsi xcb
    driver_finished "drive_controls"
    effects=$(find "$home-fx" -name '*.fx' | wc -l)
    started=-1
    if [ -f "$log.cache-start" ]; then started=$(cat "$log.cache-start"); fi
    ended=$(find "$home/.cache/vkdaedalus" -name '*.vkbfx' | wc -l)
    off=$(count 'debug: effects off$' "$log")
    on=$(count 'debug: effects on$' "$log")
    if ! why=$(exited_cleanly "$log"); then
        record FAIL "effect toggle and reloads" "$why"
    elif ! why=$(validation_verdict "$log" "$control"); then
        record FAIL "effect toggle and reloads: validation" "$why"
    elif [ "$(count 'vkDaedalus err:' "$log")" -ne 0 ]; then
        record FAIL "effect toggle and reloads" "$(grep -m1 'vkDaedalus err:' "$log")"
    elif [ "$off" -ne 1 ] || [ "$on" -ne 1 ] \
        || [ "$(grep -n -m1 'debug: effects off$' "$log" | cut -d: -f1)" -gt "$(grep -n -m1 'debug: effects on$' "$log" | cut -d: -f1)" ]; then
        record FAIL "effect toggle and reloads" "two presses of $TOGGLE_KEY gave $off off and $on on"
    elif [ "$(count 'reload key pressed' "$log")" -ne 1 ]; then
        record FAIL "effect toggle and reloads" "$RELOAD_KEY was seen $(count 'reload key pressed' "$log") times"
    elif [ "$(count 'config file changed detected' "$log")" -ne 4 ]; then
        record FAIL "effect toggle and reloads" "three edits to the watched profile and one to the game's own file were seen $(count 'config file changed detected' "$log") times"
    elif [ "$(count 'Safe Anti-Cheat is off for this game' "$log")" -lt 1 ]; then
        record FAIL "effect toggle and reloads" "an edit writing safeAntiCheat = false into configs/vkcube.conf never reached the layer, so a game's own file edited mid-game is not what runs"
    elif [ "$(count 'config reload produced empty options, restoring previous' "$log")" -ne 1 ]; then
        record FAIL "effect toggle and reloads" "emptying the watched config was refused $(count 'config reload produced empty options, restoring previous' "$log") times, want once"
    elif [ "$(count 'effects reloaded successfully' "$log")" -ne 5 ] || [ "$(count 'creating effect 0: cas' "$log")" -ne 6 ]; then
        record FAIL "effect toggle and reloads" "$(count 'effects reloaded successfully' "$log") reloads and $(count 'creating effect 0: cas' "$log") chain builds, want 5 and 6"
    elif [ "$(count 'creating effect 8: rcas' "$log")" -lt 1 ] || [ "$(count 'grew the effect image pool' "$log")" -lt 1 ]; then
        record FAIL "effect toggle and reloads" "an edit adding rcas to effects built it $(count 'creating effect 8: rcas' "$log") times and grew the image pool $(count 'grew the effect image pool' "$log") times; the chain kept the overlay's selection instead of the file's"
    elif [ "$started" -lt "$effects" ] || [ "$started" -gt $((2 * effects)) ]; then
        record FAIL "effect toggle and reloads" "the first frame left $started shader cache entries for $effects ReShade effects, want a parameter read and at most one swapchain build each, so the warm-up and the effects compute different cache keys"
    elif [ "$ended" -ne "$started" ]; then
        record FAIL "effect toggle and reloads" "the reloads took the shader cache from $started to $ended entries, so a reload computes a cache key the first build did not"
    else
        record PASS "effect toggle and reloads" "$TOGGLE_KEY turned the effects off and on; $RELOAD_KEY, named in lower case in settings.conf, two edits to the watched profile, one to the game's own file and emptying the profile each rebuilt the chain once, the game file's turning Safe Anti-Cheat off in the layer, the edit adding rcas growing it, the empty file keeping the previous settings"
    fi
}

plain_verdict() {
    local log="$1" control="$2" why
    if ! why=$(exited_cleanly "$log"); then
        echo "$why"
        return 1
    fi
    if ! why=$(validation_verdict "$log" "$control"); then
        echo "$why"
        return 1
    fi
    if [ "$(count 'vkDaedalus err:' "$log")" -ne 0 ]; then
        grep -m1 'vkDaedalus err:' "$log"
        return 1
    fi
}

resize_with_overlay_open() {
    local log="$1" window
    wait_for_line 'present cycle' 30 "$log" || { echo "the app never reached 30 presents" >"$log.driver"; return 0; }
    hold_key_until "$OVERLAY_KEY" 'overlay opened' 1 "$log" || { echo "the overlay never opened while its key was held" >"$log.driver"; return 0; }
    window=$(DISPLAY="$PRIVATE_DISPLAY" xdotool search --onlyvisible --name '' | tail -1) || true
    if [ -z "$window" ]; then
        echo "no visible window on $PRIVATE_DISPLAY to resize" >"$log.driver"
        return 0
    fi
    if ! DISPLAY="$PRIVATE_DISPLAY" xdotool windowsize "$window" 640 400; then
        echo "xdotool could not resize window $window" >"$log.driver"
        return 0
    fi
    echo "resized window $window to 640x400" >"$log.driver"
}

lane_config() {
    local icd="$1" dir="$2" control="$2/overlay-control.log" log home why fx fx_name driver mode probe_control unexpected unread_window read_window timed

    log="$dir/config-empty.log" home="$dir/config-empty-home"
    mkdir -p "$home"
    run "$log" "$home" "$icd" "$BASALT:$VALIDATION" 1 1 -- vkcube --c "$CONFIG_FRAMES" --wsi xcb
    if ! why=$(plain_verdict "$log" "$control"); then
        record FAIL "a first run needs no config of its own" "$why"
    elif [ "$(count 'no vkdaedalus.conf found' "$log")" -ne 0 ] || [ "$(count 'base config: .*/share/vkdaedalus/vkdaedalus\.conf$' "$log")" -lt 1 ]; then
        record FAIL "a first run needs no config of its own" "in an empty home the layer said: $(grep -m1 -E 'no vkdaedalus.conf found|base config:' "$log" || echo nothing)"
    else
        record PASS "a first run needs no config of its own" "in an empty home the layer read the installed default config and ran"
    fi

    log="$dir/config-disabled.log" home="$dir/config-disabled-home"
    fresh_home "$home"
    printf 'disabledEffects = smaa\nuniform_smoke = %s/gone/uniform_smoke.fx\n' "$home" >>"$home/.config/vkdaedalus/vkdaedalus.conf"
    run "$log" "$home" "$icd" "$BASALT:$VALIDATION" 1 1 -- vkcube --c "$CONFIG_FRAMES" --wsi xcb
    if ! why=$(plain_verdict "$log" "$control"); then
        record FAIL "a disabled effect stays out" "$why"
    elif [ "$(count 'creating effect [0-9]+: smaa' "$log")" -ne 0 ] || [ "$(count 'active effect count: 7$' "$log")" -lt 1 ]; then
        record FAIL "a disabled effect stays out" "smaa built $(count 'creating effect [0-9]+: smaa' "$log") times, $(count 'active effect count: 7$' "$log") chains of seven"
    else
        record PASS "a disabled effect stays out" "disabledEffects = smaa built the other seven"
    fi
    if [ "$(count 'uniform_smoke: its saved file .*/gone/uniform_smoke\.fx is gone, so .*/uniform_smoke\.fx is used' "$log")" -lt 1 ] ||
        [ "$(count 'creating effect [0-9]+: uniform_smoke' "$log")" -lt 1 ]; then
        record FAIL "a saved effect file that is gone is said" "uniform_smoke saved at a missing file: $(grep -m1 'uniform_smoke: its saved file' "$log" || echo 'no warning'), built $(count 'creating effect [0-9]+: uniform_smoke' "$log") times"
    else
        record PASS "a saved effect file that is gone is said" "uniform_smoke saved at a missing file ran from its include folder and the log named both"
    fi

    log="$dir/config-limit.log" home="$dir/config-limit-home"
    fresh_home "$home" cas:smaa:lut
    printf 'maxEffects = 2\n' >"$home/.config/vkdaedalus/settings.conf"
    run "$log" "$home" "$icd" "$BASALT:$VALIDATION" 1 1 -- vkcube --c "$CONFIG_FRAMES" --wsi xcb
    if ! why=$(plain_verdict "$log" "$control"); then
        record FAIL "the effect limit holds from launch" "$why"
    elif [ "$(count 'creating effect [0-9]+: lut' "$log")" -ne 0 ] || [ "$(count 'from lut on are left out: past the limit of 2 effects' "$log")" -lt 1 ]; then
        record FAIL "the effect limit holds from launch" "with three effects and a limit of two, lut was built $(count 'creating effect [0-9]+: lut' "$log") times and the layer said: $(grep -m1 'left out' "$log" || echo nothing)"
    else
        record PASS "the effect limit holds from launch" "with three effects and a limit of two, cas and smaa ran and lut was left out with the reason"
    fi

    log="$dir/config-vram.log" home="$dir/config-vram-home"
    fresh_home "$home"
    printf 'vramSoftLimitMB = 1\n' >>"$home/.config/vkdaedalus/vkdaedalus.conf"
    run "$log" "$home" "$icd" "$BASALT:$VALIDATION" 1 1 -- vkcube --c "$CONFIG_FRAMES" --wsi xcb
    if ! why=$(plain_verdict "$log" "$control"); then
        record FAIL "the VRAM limit is a target" "$why"
    elif [ "$(count 'creating effect [0-9]+: ' "$log")" -ne 8 ] || [ "$(count 'MiB of video memory, above the 1 MiB asked for' "$log")" -ne 1 ]; then
        record FAIL "the VRAM limit is a target" "$(count 'creating effect [0-9]+: ' "$log") effects built, $(count 'MiB of video memory, above the 1 MiB asked for' "$log") warnings, want 8 and 1"
    else
        record PASS "the VRAM limit is a target" "a 1 MiB limit was reported once and every effect was still built"
    fi

    log="$dir/config-teardown.log" home="$dir/config-teardown-home"
    fresh_home "$home"
    printf 'overlayKey = %s\n' "$OVERLAY_KEY" >"$home/.config/vkdaedalus/settings.conf"
    background press_overlay_key "$log"
    driver=$!
    run "$log" "$home" "$icd" "$BASALT:$VALIDATION" 1 1 -- vkcube --c "$CONFIG_FRAMES" --wsi xcb
    if ! wait "$driver"; then echo "the overlay key was never pressed" >"$log.driver"; fi
    if ! why=$(plain_verdict "$log" "$control"); then
        record FAIL "input comes back at teardown" "$why"
    elif [ "$(count 'Input grabbed for overlay' "$log")" -ne 1 ] || [ "$(count 'overlay closed' "$log")" -ne 0 ] || [ "$(count 'Input released from overlay' "$log")" -ne 1 ]; then
        record FAIL "input comes back at teardown" "grabbed $(count 'Input grabbed for overlay' "$log"), closed $(count 'overlay closed' "$log"), released $(count 'Input released from overlay' "$log"), want 1, 0, 1"
    else
        record PASS "input comes back at teardown" "the overlay left open with input blocked released its grab when the game exited"
    fi

    log="$dir/config-bypass.log" home="$dir/config-bypass-home"
    fresh_home "$home"
    mkdir -p "$home/.config/vkdaedalus/configs" "$home/.config/vkdaedalus/profiles"
    printf 'vkcube=none\n' >"$home/.config/vkdaedalus/configs/.active_profiles"
    printf 'safeAntiCheat = false\n' >"$home/.config/vkdaedalus/configs/vkcube.conf"
    printf 'depthCapture = on\n' >"$home/.config/vkdaedalus/settings.conf"
    run "$log" "$home" "$icd" "$BASALT:$VALIDATION" 1 1 -- vkcube --c "$CONFIG_FRAMES" --wsi wayland
    if ! why=$(plain_verdict "$log" "$control"); then
        record FAIL "a bypassed program is left alone" "$why"
    elif [ "$(count 'the layer stays out of this program' "$log")" -lt 1 ] || [ "$(count 'vkCreateWaylandSurfaceKHR|vkCreateSwapchainKHR|present cycle|created fake swapchain images|detected depth image' "$log")" -ne 0 ]; then
        record FAIL "a bypassed program is left alone" "stays-out lines $(count 'the layer stays out of this program' "$log"); hooked anyway: $(grep -m1 -E 'vkCreateWaylandSurfaceKHR|vkCreateSwapchainKHR|present cycle|created fake swapchain images|detected depth image' "$log")"
    else
        record PASS "a bypassed program is left alone" "vkcube=none with depth capture on and its own file saying safeAntiCheat = false: no surface, swapchain, present or depth hook ran"
    fi

    log="$dir/config-other-game.log" home="$dir/config-other-game-home"
    fresh_home "$home"
    run "$log" "$home" "$icd" "$BASALT:$VALIDATION" 1 1 -- VKDAEDALUS_RUN_PROGRAM=OtherGame.exe VKDAEDALUS_RUN_EXECUTABLE="$dir/games/Other/OtherGame.exe" \
        vkcube --c "$CONFIG_FRAMES" --wsi wayland
    if ! why=$(plain_verdict "$log" "$control"); then
        record FAIL "a program vkdaedalus-run did not start stays out" "$why"
    elif [ "$(count 'is not the game vkdaedalus-run started \(OtherGame.exe' "$log")" -lt 1 ] || [ "$(count 'vkCreateWaylandSurfaceKHR|vkCreateSwapchainKHR|present cycle|created fake swapchain images' "$log")" -ne 0 ]; then
        record FAIL "a program vkdaedalus-run did not start stays out" "stays-out lines $(count 'is not the game vkdaedalus-run started' "$log"); hooked anyway: $(grep -m1 -E 'vkCreateWaylandSurfaceKHR|vkCreateSwapchainKHR|present cycle|created fake swapchain images' "$log")"
    else
        record PASS "a program vkdaedalus-run did not start stays out" "vkcube under a vkdaedalus-run that named OtherGame.exe ran no surface, swapchain or present hook"
    fi


    log="$dir/config-depth.log" home="$dir/config-depth-home" fx="$dir/config-depth-home-fx"
    fresh_home "$home" cas:depth_semantic_smoke:smaa
    cp "$VKB_FIXTURES/effects/depth_semantic_smoke.fx" "$fx/"
    printf 'depth_semantic_smoke = %s\n' "$fx/depth_semantic_smoke.fx" >>"$home/.config/vkdaedalus/vkdaedalus.conf"
    printf 'depthCapture = on\n' >"$home/.config/vkdaedalus/settings.conf"
    mkdir -p "$home/.config/vkdaedalus/configs" "$home/.config/vkdaedalus/profiles"
    printf 'safeAntiCheat = false\n' >"$home/.config/vkdaedalus/configs/vkcube.conf"
    run "$log" "$home" "$icd" "$BASALT:$VALIDATION" 1 1 -- vkcube --c "$CONFIG_FRAMES" --wsi xcb
    if ! why=$(plain_verdict "$log" "$control"); then
        record FAIL "depth capture feeds a depth effect" "$why"
    elif [ "$(count 'detected depth image' "$log")" -lt 1 ] || [ "$(count 'created depth image view' "$log")" -lt 1 ] || [ "$(count 'creating effect [0-9]+: depth_semantic_smoke' "$log")" -ne 1 ]; then
        record FAIL "depth capture feeds a depth effect" "depth images seen $(count 'detected depth image' "$log"), views made $(count 'created depth image view' "$log"), depth effect built $(count 'creating effect [0-9]+: depth_semantic_smoke' "$log")"
    else
        record PASS "depth capture feeds a depth effect" "vkcube's depth image was captured, viewed and sampled by a DEPTH-semantic effect, validation clean"
    fi

    log="$dir/config-anticheat.log" home="$dir/config-anticheat-home" fx="$dir/config-anticheat-home-fx"
    fresh_home "$home" cas:depth_semantic_smoke:smaa
    cp "$VKB_FIXTURES/effects/depth_semantic_smoke.fx" "$fx/"
    printf 'depth_semantic_smoke = %s\n' "$fx/depth_semantic_smoke.fx" >>"$home/.config/vkdaedalus/vkdaedalus.conf"
    printf 'depthCapture = on\n' >"$home/.config/vkdaedalus/settings.conf"
    run "$log" "$home" "$icd" "$BASALT:$VALIDATION" 1 1 -- vkcube --c "$CONFIG_FRAMES" --wsi xcb
    if ! why=$(plain_verdict "$log" "$control"); then
        record FAIL "safe anti-cheat keeps depth out" "$why"
    elif [ "$(count 'detected depth image' "$log")" -ne 0 ] || [ "$(count 'Safe Anti-Cheat keeps depth_semantic_smoke out of the chain' "$log")" -lt 1 ]; then
        record FAIL "safe anti-cheat keeps depth out" "depth images seen $(count 'detected depth image' "$log"), depth effect refused $(count 'Safe Anti-Cheat keeps depth_semantic_smoke out of the chain' "$log") time(s), with the overlay never opened"
    elif ! grep -qx 'safeAntiCheat = true' "$home/.config/vkdaedalus/configs/vkcube.conf"; then
        record FAIL "safe anti-cheat keeps depth out" "the file made for the new game, configs/vkcube.conf, does not say safeAntiCheat = true"
    else
        record PASS "safe anti-cheat keeps depth out" "a new game, with depth capture on and no file of its own: Safe Anti-Cheat on by default, no depth image captured, the depth-reading effect left out, validation clean"
    fi

    local moved="the move to shared profiles at the first start"
    log="$dir/config-shared-profiles.log" home="$dir/config-shared-profiles-home"
    fresh_home "$home" cas
    mkdir -p "$home/.config/vkdaedalus/configs"
    printf 'effects = cas\nsafeAntiCheat = false\n' >"$home/.config/vkdaedalus/configs/vkcube.conf"
    printf 'effects = smaa\n' >"$home/.config/vkdaedalus/configs/vkcube@night.conf"
    printf 'vkcube=night\n' >"$home/.config/vkdaedalus/configs/.active_profiles"
    run "$log" "$home" "$icd" "$BASALT:$VALIDATION" 1 1 -- vkcube --c "$CONFIG_FRAMES" --wsi xcb
    if ! why=$(plain_verdict "$log" "$control"); then
        record FAIL "$moved" "$why"
    elif [ "$(count 'moved 2 profile files to .*/profiles,' "$log")" -ne 1 ] || ! file_says "$log" 'hot-reload initialized, config: .*/profiles/night\.conf$' || [ "$(count 'creating effect 0: smaa' "$log")" -lt 1 ]; then
        record FAIL "$moved" "moved $(count 'moved 2 profile files to .*/profiles,' "$log") times, ran $(sed -n 's/.*hot-reload initialized, config: //p' "$log" | sed -n 1p), smaa built $(count 'creating effect 0: smaa' "$log") times"
    elif [ ! -f "$home/.config/vkdaedalus/configs.before-shared-profiles/vkcube@night.conf" ] || [ -e "$home/.config/vkdaedalus/configs/vkcube@night.conf" ]; then
        record FAIL "$moved" "the backup holds vkcube@night.conf: $([ -f "$home/.config/vkdaedalus/configs.before-shared-profiles/vkcube@night.conf" ] && echo yes || echo no); the old file is still in configs/: $([ -e "$home/.config/vkdaedalus/configs/vkcube@night.conf" ] && echo yes || echo no)"
    elif ! file_says "$home/.config/vkdaedalus/configs/vkcube.conf" '^safeAntiCheat = true$' || [ "$(count 'Safe Anti-Cheat is on' "$log")" -lt 1 ]; then
        record FAIL "$moved" "vkcube ran night, which never turned Safe Anti-Cheat off, yet its own file says $(grep -m1 '^safeAntiCheat' "$home/.config/vkdaedalus/configs/vkcube.conf" || echo nothing)"
    else
        record PASS "$moved" "vkcube's own file and its night profile moved to profiles/ with a backup of configs/; vkcube ran night, and kept Safe Anti-Cheat on, as night had it"
    fi

    log="$dir/config-shipped.log" home="$dir/config-shipped-home" fx="$dir/config-shipped-home-fx"
    local shipped
    shipped=$(mktemp -d "$RUNNABLE/shipped-game.XXXXXX")
    mkdir -p "$shipped/EasyAntiCheat"
    : >"$shipped/EasyAntiCheat/EasyAntiCheat_x64.dll"
    cp "$(command -v vkcube)" "$shipped/vkcube"
    fresh_home "$home" cas:depth_semantic_smoke:smaa
    cp "$VKB_FIXTURES/effects/depth_semantic_smoke.fx" "$fx/"
    printf 'depth_semantic_smoke = %s\n' "$fx/depth_semantic_smoke.fx" >>"$home/.config/vkdaedalus/vkdaedalus.conf"
    printf 'depthCapture = on\noverlayKey = %s\n' "$OVERLAY_KEY" >"$home/.config/vkdaedalus/settings.conf"
    mkdir -p "$home/.config/vkdaedalus/configs" "$home/.config/vkdaedalus/profiles"
    printf 'safeAntiCheat = false\n' >"$home/.config/vkdaedalus/configs/vkcube.conf"
    background drive_locked_checkbox "$log" "$dir/config-anticheat.map"
    driver=$!
    run "$log" "$home" "$icd" "$BASALT:$VALIDATION" 1 1 -- env VKDAEDALUS_UI_MAP="$dir/config-anticheat.map" "$shipped/vkcube" --c "$UI_FRAMES" --wsi xcb
    driver_finished "drive_locked_checkbox"
    while IFS=$'\t' read -r step name detail; do
        record "$step" "$name" "$detail"
    done <"$log.ui"
    if ! why=$(plain_verdict "$log" "$control"); then
        record FAIL "a shipped anti-cheat locks Safe Anti-Cheat on" "$why"
    elif [ "$(count 'Safe Anti-Cheat is locked on, since EasyAntiCheat ships with this game \(EasyAntiCheat/EasyAntiCheat_x64\.dll\)' "$log")" -lt 1 ]; then
        record FAIL "a shipped anti-cheat locks Safe Anti-Cheat on" "the layer never reported the lock: $(grep -m1 -E 'Safe Anti-Cheat is' "$log")"
    elif [ "$(count 'detected depth image' "$log")" -ne 0 ] || [ "$(count 'Safe Anti-Cheat keeps depth_semantic_smoke out of the chain' "$log")" -lt 1 ]; then
        record FAIL "a shipped anti-cheat locks Safe Anti-Cheat on" "with the profile saying false: depth images seen $(count 'detected depth image' "$log"), depth effect refused $(count 'Safe Anti-Cheat keeps depth_semantic_smoke out of the chain' "$log") time(s)"
    else
        record PASS "a shipped anti-cheat locks Safe Anti-Cheat on" "a game folder holding EasyAntiCheat/EasyAntiCheat_x64.dll kept Safe Anti-Cheat on although its profile says false: no depth image captured, the depth-reading effect left out, validation clean"
    fi
    rm -rf "${shipped:?}"

    log="$dir/config-shared.log" home="$dir/config-shared-home" fx="$dir/config-shared-home-fx"
    fresh_home "$home" shared_texture_producer:shared_texture_consumer:shared_texture_mismatch:shared_texture_storage:two_pass_smoke
    for fx_name in shared_texture_producer shared_texture_consumer shared_texture_mismatch shared_texture_storage; do
        cp "$VKB_FIXTURES/language/$fx_name.fx" "$fx/"
        printf '%s = %s\n' "$fx_name" "$fx/$fx_name.fx" >>"$home/.config/vkdaedalus/vkdaedalus.conf"
    done
    cp "$VKB_FIXTURES/effects/two_pass_smoke.fx" "$fx/"
    printf 'two_pass_smoke = %s\n' "$fx/two_pass_smoke.fx" >>"$home/.config/vkdaedalus/vkdaedalus.conf"
    run "$log" "$home" "$icd" "$BASALT:$VALIDATION" 1 1 -- vkcube --c "$CONFIG_FRAMES" --wsi xcb
    if ! why=$(exited_cleanly "$log"); then
        record FAIL "a texture two effects share" "$why"
    elif ! why=$(validation_verdict "$log" "$control"); then
        record FAIL "a texture two effects share: validation" "$why"
    elif [ "$(count 'created shared effect texture V__SharedTextureProbe__PublishedColor$' "$log")" -ne 1 ] || [ "$(count 'creating effect [0-9]+: shared_texture_consumer' "$log")" -ne 1 ]; then
        record FAIL "a texture two effects share" "the probe texture was created $(count 'created shared effect texture V__SharedTextureProbe__PublishedColor$' "$log") times, consumer built $(count 'creating effect [0-9]+: shared_texture_consumer' "$log"), want 1 and 1"
    elif [ "$(count 'vkDaedalus err:' "$log")" -ne 1 ] || [ "$(count 'vkDaedalus err: .*incompatible shared effect texture declaration: V__SharedTextureProbe__PublishedColor$' "$log")" -ne 1 ]; then
        record FAIL "a texture two effects share" "want exactly one error, refusing the RG8 redeclaration; got: $(grep -m2 'vkDaedalus err:' "$log")"
    elif [ "$(count 'effect texture V__SharedTextureProbe__PublishedColor needs usage the shared image was not created with; using a private copy' "$log")" -ne 1 ]; then
        record FAIL "a texture two effects share" "the storage writer got $(count 'using a private copy' "$log") private copies, want 1"
    elif [ "$(count 'creating effect [0-9]+: two_pass_smoke' "$log")" -ne 1 ]; then
        record FAIL "a texture two effects share" "the two-pass effect was built $(count 'creating effect [0-9]+: two_pass_smoke' "$log") times, want 1"
    else
        record PASS "a texture two effects share" "one shared image for producer and consumer, the RG8 redeclaration refused, the storage writer given its own copy, a second pass sampling what the first rendered, validation clean through teardown"
    fi

    log="$dir/config-api.log" home="$dir/config-api-home"
    fresh_home "$home"
    printf 'depthCapture = on\n' >"$home/.config/vkdaedalus/settings.conf"
    mkdir -p "$home/.config/vkdaedalus/configs" "$home/.config/vkdaedalus/profiles"
    printf 'safeAntiCheat = false\n' >"$home/.config/vkdaedalus/configs/api-probe.conf"
    run "$log" "$home" "$icd" "$BASALT:$VALIDATION" 1 1 -- api-probe
    if ! why=$(plain_verdict "$log" "$control"); then
        record FAIL "valid API uses vkcube never makes" "$why"
    elif [ "$(count 'api-probe: null swapchain and image destroyed' "$log.stdout")" -ne 1 ]; then
        record FAIL "valid API uses vkcube never makes" "the probe did not finish: $(tail -1 "$log.stdout")"
    elif [ "$(count 'created depth image view' "$log")" -ne 1 ]; then
        record FAIL "valid API uses vkcube never makes" "a depth image bound with vkBindImageMemory2 got $(count 'created depth image view' "$log") depth views from depth capture, want 1"
    else
        record PASS "valid API uses vkcube never makes" "a null swapchain and image destroyed, a transient depth attachment left alone and a depth image bound with vkBindImageMemory2 captured by depth capture, the application's feature struct given back unchanged, a device made and destroyed with the application's allocation callbacks, validation clean"
    fi

    log="$dir/config-depth-destroy.log" home="$dir/config-depth-destroy-home" fx="$dir/config-depth-destroy-home-fx" probe_control="$dir/config-depth-destroy-control.log"
    fresh_home "$home-control"
    run "$probe_control" "$home-control" "$icd" "$VALIDATION" 0 1 -- two-swapchain-probe "$CONFIG_FRAMES" depth
    if [ "$(cat "$probe_control.rc")" = 77 ]; then
        record SKIP "a destroyed depth image re-records only what read it" "$(tail -1 "$probe_control.stdout")"
    elif ! why=$(exited_cleanly "$probe_control"); then
        record FAIL "a destroyed depth image re-records only what read it" "the probe fails without the layer: $why $(tail -1 "$probe_control")"
    else
        fresh_home "$home" cas:depth_semantic_smoke
        cp "$VKB_FIXTURES/effects/depth_semantic_smoke.fx" "$fx/"
        printf 'depth_semantic_smoke = %s\n' "$fx/depth_semantic_smoke.fx" >>"$home/.config/vkdaedalus/vkdaedalus.conf"
        printf 'depthCapture = on\n' >"$home/.config/vkdaedalus/settings.conf"
        mkdir -p "$home/.config/vkdaedalus/configs" "$home/.config/vkdaedalus/profiles"
        printf 'safeAntiCheat = false\n' >"$home/.config/vkdaedalus/configs/two-swapchain-probe.conf"
        run "$log" "$home" "$icd" "$BASALT:$VALIDATION" 1 1 -- two-swapchain-probe "$CONFIG_FRAMES" depth
        unread_window=$(awk '/two-swapchain-probe: destroying a depth image no effect reads/ { on = 1 } on && /reallocated CommandBuffers for swapchain/ { n++ } on && /two-swapchain-probe: destroyed it/ { print n + 0; exit }' "$log")
        read_window=$(awk '/two-swapchain-probe: destroying the depth image the effects read/ { on = 1 } on && /reallocated CommandBuffers for swapchain/ { n++ } on && /two-swapchain-probe: destroyed it/ { print n + 0; exit }' "$log")
        if ! why=$(plain_verdict "$log" "$probe_control"); then
            record FAIL "a destroyed depth image re-records only what read it" "$why"
        elif ! grep -q 'two depth images bound and destroyed between them$' "$log.stdout"; then
            record FAIL "a destroyed depth image re-records only what read it" "the probe did not finish: $(tail -1 "$log.stdout")"
        elif [ "$(count 'created depth image view' "$log")" -ne 2 ]; then
            record FAIL "a destroyed depth image re-records only what read it" "depth capture made $(count 'created depth image view' "$log") depth views, want 2"
        elif [ "$unread_window" != 0 ] || [ "${read_window:-0}" -lt 1 ]; then
            record FAIL "a destroyed depth image re-records only what read it" "destroying the depth image no effect reads re-recorded ${unread_window:-no window} swapchain(s), want 0; destroying the one they read re-recorded ${read_window:-no window}, want at least 1"
        else
            record PASS "a destroyed depth image re-records only what read it" "two swapchains on one device: destroying an unread depth image re-recorded nothing, destroying the one the effects read re-recorded $read_window, validation clean"
        fi
    fi

    for mode in formats compute-only; do
        log="$dir/config-swapchain-$mode.log" home="$dir/config-swapchain-$mode-home" probe_control="$dir/config-swapchain-$mode-control.log"
        fresh_home "$home"
        fresh_home "$home-control"
        run "$probe_control" "$home-control" "$icd" "$VALIDATION" 0 1 -- swapchain-probe "$mode"
        if [ "$(cat "$probe_control.rc")" = 77 ]; then
            record SKIP "swapchain probe: $mode" "$(tail -1 "$probe_control.stdout")"
            continue
        fi
        if ! why=$(exited_cleanly "$probe_control"); then
            record FAIL "swapchain probe: $mode" "the probe fails without the layer: $why $(tail -1 "$probe_control")"
            continue
        fi
        run "$log" "$home" "$icd" "$BASALT:$VALIDATION" 1 1 -- swapchain-probe "$mode"
        unexpected=$(awk '/vkDaedalus err:/ && !/Did not find a graphics queue/ { print; exit }' "$log")
        if ! why=$(exited_cleanly "$log"); then
            record FAIL "swapchain probe: $mode" "$why $(tail -1 "$log.stdout")"
        elif ! why=$(validation_verdict "$log" "$probe_control"); then
            record FAIL "swapchain probe: $mode" "$why"
        elif [ "$mode" = formats ] && [ "$(count 'vkDaedalus err:' "$log")" -ne 0 ]; then
            record FAIL "swapchain probe: $mode" "$(grep -m1 'vkDaedalus err:' "$log")"
        elif [ -n "$unexpected" ]; then
            record FAIL "swapchain probe: $mode" "$unexpected"
        else
            record PASS "swapchain probe: $mode" "$(tail -1 "$log.stdout" | sed 's/^swapchain-probe: //'), validation clean"
        fi
    done

    log="$dir/config-declarations.log" home="$dir/config-declarations-home" fx="$dir/config-declarations-home-fx"
    fresh_home "$home" cas:texture_declarations:texture_unknown_format:unknown_source:texture_dimensions:smaa
    for fx_name in texture_declarations texture_unknown_format unknown_source texture_dimensions; do
        cp "$VKB_FIXTURES/effects/$fx_name.fx" "$fx/"
        printf '%s = %s\n' "$fx_name" "$fx/$fx_name.fx" >>"$home/.config/vkdaedalus/vkdaedalus.conf"
    done
    run "$log" "$home" "$icd" "$BASALT:$VALIDATION" 1 1 -- vkcube --c "$CONFIG_FRAMES" --wsi xcb
    if ! why=$(exited_cleanly "$log"); then
        record FAIL "texture declarations reach Vulkan valid" "$why"
    elif ! why=$(validation_verdict "$log" "$control"); then
        record FAIL "texture declarations reach Vulkan valid: validation" "$why"
    elif [ "$(count 'creating effect [0-9]+: texture_declarations' "$log")" -ne 1 ]; then
        record FAIL "texture declarations reach Vulkan valid" "the effect was built $(count 'creating effect [0-9]+: texture_declarations' "$log") times"
    elif [ "$(count 'creating effect [0-9]+: texture_dimensions' "$log")" -ne 1 ]; then
        record FAIL "texture declarations reach Vulkan valid" "texture_dimensions was built $(count 'creating effect [0-9]+: texture_dimensions' "$log") times"
    elif [ "$(count 'vkDaedalus err:' "$log")" -ne 2 ] || [ "$(count 'vkDaedalus err: .*texture format 5 has no Vulkan equivalent here' "$log")" -ne 1 ] \
        || [ "$(count 'vkDaedalus err: +texture solid.png: only a .cube LUT fills a texture deeper than one layer' "$log")" -ne 1 ]; then
        record FAIL "texture declarations reach Vulkan valid" "want exactly two errors, refusing Format = 5 and an image file for a 4x4x4 texture; got: $(grep -m3 'vkDaedalus err:' "$log")"
    elif [ "$(count 'vkDaedalus warn: .*OverlayOpen asks for source "overlay_open", which this layer does not provide' "$log")" -ne 1 ]; then
        record FAIL "texture declarations reach Vulkan valid" "a uniform asking for the unprovided source overlay_open was not reported once"
    elif [ "$(count 'sampler DeclMipsSampler: MipLODBias 100\.0+ is held at' "$log")" -lt 1 ]; then
        record FAIL "texture declarations reach Vulkan valid" "MipLODBias = 100 was never reported held at this GPU's limit"
    elif [ "$(count 'texture DeclIntMips holds integers, so its mip levels are made from the nearest texel' "$log")" -lt 1 ] \
        || [ "$(count 'sampler DeclSignedIntsSampler reads integers, which have no linear filter' "$log")" -lt 1 ]; then
        record FAIL "texture declarations reach Vulkan valid" "the integer render target's mip levels or the integer sampler asking for linear filtering were not taken to the nearest texel"
    else
        record PASS "texture declarations reach Vulkan valid" "RG11B10F, RGB10A2, RGBA32I and RGBA32U textures, a mipmapped render target sampled at level 2, an over-long mip chain clamped, 1D and 3D textures sampled and stored as declared, an image file for a 4x4x4 texture refused, integer mip levels and an integer sampler asking for linear filtering taken to the nearest texel, Format = 5 refused, an unprovided uniform source reported, validation clean"
    fi

    log="$dir/config-limits.log" home="$dir/config-limits-home" fx="$dir/config-limits-home-fx"
    fresh_home "$home" cas:texture_too_wide:compute_group_too_large:dispatch_too_many:sampler_lod_inverted:smaa
    for fx_name in texture_too_wide compute_group_too_large dispatch_too_many sampler_lod_inverted; do
        cp "$VKB_FIXTURES/effects/$fx_name.fx" "$fx/"
        printf '%s = %s\n' "$fx_name" "$fx/$fx_name.fx" >>"$home/.config/vkdaedalus/vkdaedalus.conf"
    done
    run "$log" "$home" "$icd" "$BASALT:$VALIDATION" 1 1 -- vkcube --c "$CONFIG_FRAMES" --wsi xcb
    if ! why=$(exited_cleanly "$log"); then
        record FAIL "a GPU's limits leave an effect out there" "$why"
    elif ! why=$(validation_verdict "$log" "$control"); then
        record FAIL "a GPU's limits leave an effect out there: validation" "$why"
    elif [ "$(count 'vkDaedalus warn: +texture_too_wide is left out on this GPU: its texture TooWideTex .* is larger than this GPU makes' "$log")" -lt 1 ] \
        || [ "$(count 'vkDaedalus warn: +compute_group_too_large is left out on this GPU: its compute shader .* threads per group' "$log")" -lt 1 ] \
        || [ "$(count 'vkDaedalus warn: +dispatch_too_many is left out on this GPU: its compute pass .* dispatches 2147483647 groups' "$log")" -lt 1 ]; then
        record FAIL "a GPU's limits leave an effect out there" "want texture_too_wide, compute_group_too_large and dispatch_too_many each left out with its reason; got: $(grep -m3 'is left out on this GPU' "$log")"
    elif [ "$(count 'vkDaedalus err:' "$log")" -ne 1 ] || [ "$(count 'vkDaedalus err: .*sampler_lod_inverted: sampler InvertedSampler asks for MinLOD 4\.0+ above MaxLOD 1\.0+' "$log")" -ne 1 ]; then
        record FAIL "a GPU's limits leave an effect out there" "want exactly one error, the inverted LOD range refused on every GPU; got: $(grep -m2 'vkDaedalus err:' "$log")"
    elif [ "$(count 'creating effect [0-9]+: smaa' "$log")" -ne 1 ]; then
        record FAIL "a GPU's limits leave an effect out there" "the chain after the refused effects was built $(count 'creating effect [0-9]+: smaa' "$log") times, want 1"
    else
        record PASS "a GPU's limits leave an effect out there" "a texture wider than the GPU makes, a thread group larger than it runs and more groups than it dispatches each left out on this GPU with its reason, an inverted LOD range refused, the rest of the chain built, validation clean"
    fi

    log="$dir/config-failed.log" home="$dir/config-failed-home"
    fresh_home "$home" cas:smaa:lut
    sed -i "s|^lutFile = .*|lutFile = $home/no-such-lut.cube|" "$home/.config/vkdaedalus/vkdaedalus.conf"
    printf 'effectGpuTiming = true\n' >"$home/.config/vkdaedalus/settings.conf"
    run "$log" "$home" "$icd" "$BASALT:$VALIDATION" 1 1 -- vkcube --c "$CONFIG_FRAMES" --wsi xcb
    timed=$(grep -oE 'per-effect GPU timing enabled for [0-9]+ effect' "$log" | tail -1 | grep -oE '[0-9]+') || timed=""
    if ! why=$(exited_cleanly "$log"); then
        record FAIL "a failed effect costs nothing" "$why"
    elif ! why=$(validation_verdict "$log" "$control"); then
        record FAIL "a failed effect costs nothing: validation" "$why"
    elif [ "$(count 'Failed to create built-in effect lut: ' "$log")" -lt 1 ]; then
        record FAIL "a failed effect costs nothing" "lut with a missing LUT file never failed, so the case tested nothing"
    elif [ -z "$timed" ]; then
        record SKIP "a failed effect costs nothing" "this GPU gives no timestamps, so no effect was timed"
    elif [ "$timed" != 2 ] || [ "$(count 'first per-effect GPU timing sample for lut: ' "$log")" -ne 0 ]; then
        record FAIL "a failed effect costs nothing" "lut failed, yet the chain timed $timed effects and took $(count 'first per-effect GPU timing sample for lut: ' "$log") sample(s) for lut, so it still ran a pass in its place"
    else
        record PASS "a failed effect costs nothing" "lut failed on a missing LUT file and was left out: the chain timed cas and smaa only and took no sample for lut"
    fi

    log="$dir/config-resize.log" home="$dir/config-resize-home"
    fresh_home "$home"
    printf 'overlayKey = %s\n' "$OVERLAY_KEY" >"$home/.config/vkdaedalus/settings.conf"
    background resize_with_overlay_open "$log"
    driver=$!
    run "$log" "$home" "$icd" "$BASALT:$VALIDATION" 1 1 -- vkcube --c "$FRAMES" --wsi xcb
    driver_finished "resize_with_overlay_open"
    if ! why=$(plain_verdict "$log" "$control"); then
        record FAIL "the overlay follows a resize" "$why"
    elif [ "$(count 'overlay opened' "$log")" -ne 1 ] || [ "$(count 'debounced resize reload' "$log")" -lt 1 ]; then
        record FAIL "the overlay follows a resize" "opened $(count 'overlay opened' "$log"), resize reloads $(count 'debounced resize reload' "$log"); driver: $(answer "$log.driver")"
    elif [ "$(awk '/debounced resize reload/ { seen = 1 } seen && /overlay recorded a frame/ { n++ } END { print n + 0 }' "$log")" -lt 10 ]; then
        record FAIL "the overlay follows a resize" "the overlay drew $(awk '/debounced resize reload/ { seen = 1 } seen && /overlay recorded a frame/ { n++ } END { print n + 0 }' "$log") frames after the resize"
    else
        record PASS "the overlay follows a resize" "a resize with the overlay open rebuilt the swapchain and the overlay kept drawing"
    fi
}

close_to() {
    awk -v want="$2" 'NR == 1 { split(want, w, " "); ok = NF == 7; for (i = 1; i <= 3; i++) { d = $i - w[i]; if (d < -3 || d > 3) ok = 0 } } END { exit ok ? 0 : 1 }' "$1"
}

lane_pixels() {
    local icd="$1" dir="$2" control="$2/overlay-control.log" log home fx why name chain want shot cache presser
    for name in solid output-kept bindings compute-sees-writes mips-left-alone target-one-alone global-definition global-definition-empty macro-gated; do
        log="$dir/pixels-$name.log" home="$dir/pixels-$name-home" fx="$dir/pixels-$name-home-fx" shot="$dir/pixels-$name-shots"
        case "$name" in
            solid) chain=solid_fill want="64 128 191" ;;
            output-kept) chain=solid_fill:shared_texture_producer want="64 128 191" ;;
            bindings) chain=binding_colors want="0 0 255" ;;
            compute-sees-writes) chain=compute_reads_back_buffer want="255 0 0" ;;
            mips-left-alone) chain=mips_left_alone want="0 0 0" ;;
            target-one-alone) chain=render_target_one_alone want="0 255 0" ;;
            global-definition) chain=global_definition want="0 0 255" ;;
            global-definition-empty) chain=global_definition want="255 0 0" ;;
            macro-gated) chain=macro_gated_parameter want="0 0 255" ;;
        esac
        fresh_home "$home" "$chain"
        cp "$VKB_FIXTURES/effects/solid_fill.fx" "$VKB_FIXTURES/effects/binding_colors.fx" "$VKB_FIXTURES/effects/compute_reads_back_buffer.fx" \
            "$VKB_FIXTURES/effects/mips_left_alone.fx" "$VKB_FIXTURES/effects/render_target_one_alone.fx" \
            "$VKB_FIXTURES/language/shared_texture_producer.fx" "$VKB_FIXTURES/effects/global_definition.fx" "$VKB_FIXTURES/effects/macro_gated_parameter.fx" "$fx/"
        printf 'solid_fill = %s\nbinding_colors = %s\ncompute_reads_back_buffer = %s\nmips_left_alone = %s\nrender_target_one_alone = %s\nshared_texture_producer = %s\nglobal_definition = %s\nmacro_gated_parameter = %s\n' \
            "$fx/solid_fill.fx" "$fx/binding_colors.fx" "$fx/compute_reads_back_buffer.fx" "$fx/mips_left_alone.fx" "$fx/render_target_one_alone.fx" "$fx/shared_texture_producer.fx" "$fx/global_definition.fx" "$fx/macro_gated_parameter.fx" \
            >>"$home/.config/vkdaedalus/vkdaedalus.conf"
        if [ "$name" = global-definition ]; then
            printf '@RESHADE_DEPTH_INPUT_IS_REVERSED = 0\n' >>"$home/.config/vkdaedalus/vkdaedalus.conf"
        fi
        if [ "$name" = global-definition-empty ]; then
            printf '@RESHADE_DEPTH_INPUT_IS_REVERSED =\n' >>"$home/.config/vkdaedalus/vkdaedalus.conf"
        fi
        if [ "$name" = macro-gated ]; then
            printf 'macro_gated_parameter@USE_GATE = 1\nmacro_gated_parameter.GateBlue = 1.0\n' >>"$home/.config/vkdaedalus/vkdaedalus.conf"
        fi
        for cache in cold warm; do
            rm -rf "$shot"
            mkdir -p "$shot"
            run "$log.$cache" "$home" "$icd" "$BASALT:$SCREENSHOT:$VALIDATION" 1 1 -- \
                env VK_SCREENSHOT_FRAMES=120 VK_SCREENSHOT_DIR="$shot" vkcube --c "$FRAMES" --wsi xcb
            if ! why=$(plain_verdict "$log.$cache" "$control"); then
                record FAIL "pixels: $name, $cache cache" "$why"
            elif [ "$cache" = warm ] && [ "$(count 'compiled effects: reused' "$log.$cache")" -lt 1 ]; then
                record FAIL "pixels: $name, warm cache" "the second run compiled again instead of reading the shader cache"
            elif ! pixel-probe "$shot/120.ppm" 64 >"$log.$cache.pixels" 2>&1; then
                record FAIL "pixels: $name, $cache cache" "no frame to read: $(head -1 "$log.$cache.pixels"); shots: $(ls "$shot")"
            elif ! close_to "$log.$cache.pixels" "$want"; then
                record FAIL "pixels: $name, $cache cache" "frame 120 shows $(head -1 "$log.$cache.pixels"), want $want ($chain)"
            else
                record PASS "pixels: $name, $cache cache" "$chain presents $want"
            fi
        done
    done

    log="$dir/pixels-overlay.log" home="$dir/pixels-overlay-home" fx="$dir/pixels-overlay-home-fx" shot="$dir/pixels-overlay-shots"
    fresh_home "$home" solid_fill
    cp "$VKB_FIXTURES/effects/solid_fill.fx" "$fx/"
    printf 'solid_fill = %s\n' "$fx/solid_fill.fx" >>"$home/.config/vkdaedalus/vkdaedalus.conf"
    printf 'overlayKey = %s\n' "$OVERLAY_KEY" >"$home/.config/vkdaedalus/settings.conf"
    mkdir -p "$shot"
    background press_overlay_key "$log"
    presser=$!
    run "$log" "$home" "$icd" "$BASALT:$SCREENSHOT:$VALIDATION" 1 1 -- \
        env VK_SCREENSHOT_FRAMES=540 VK_SCREENSHOT_DIR="$shot" vkcube --c 600 --wsi xcb
    if ! wait "$presser"; then
        record FAIL "pixels: the overlay reaches the screen" "the overlay never opened"
    elif ! why=$(plain_verdict "$log" "$control"); then
        record FAIL "pixels: the overlay reaches the screen" "$why"
    elif [ "$(count 'overlay recorded a frame' "$log")" -lt 60 ]; then
        record FAIL "pixels: the overlay reaches the screen" "the overlay recorded $(count 'overlay recorded a frame' "$log") frames, want at least 60"
    elif ! pixel-probe "$shot/540.ppm" 64 >"$log.pixels" 2>&1; then
        record FAIL "pixels: the overlay reaches the screen" "no frame to read: $(head -1 "$log.pixels")"
    elif ! awk 'NR == 1 { split("64 128 191", fill, " "); d = 0; for (i = 1; i <= 3; i++) d += ($i > fill[i]) ? $i - fill[i] : fill[i] - $i; ok = NF == 7 && d >= 60 } END { exit ok ? 0 : 1 }' "$log.pixels"; then
        record FAIL "pixels: the overlay reaches the screen" "with the overlay open, frame 540 shows $(head -1 "$log.pixels"), still the solid fill of 64 128 191 under it"
    else
        record PASS "pixels: the overlay reaches the screen" "with the overlay open over a solid fill of 64 128 191, the centre of frame 540 shows $(head -1 "$log.pixels" | cut -d' ' -f1-3)"
    fi

    log="$dir/pixels-timer.log" home="$dir/pixels-timer-home" fx="$dir/pixels-timer-home-fx" shot="$dir/pixels-timer-shots"
    fresh_home "$home" timer_with_initializer
    cp "$VKB_FIXTURES/effects/timer_with_initializer.fx" "$fx/"
    printf 'timer_with_initializer = %s\n' "$fx/timer_with_initializer.fx" >>"$home/.config/vkdaedalus/vkdaedalus.conf"
    mkdir -p "$shot"
    run "$log" "$home" "$icd" "$BASALT:$SCREENSHOT:$VALIDATION" 1 1 -- \
        env VK_SCREENSHOT_FRAMES=30,60 VK_SCREENSHOT_DIR="$shot" vkcube --c "$FRAMES" --wsi xcb
    if ! why=$(plain_verdict "$log" "$control"); then
        record FAIL "pixels: timer" "$why"
    elif ! pixel-probe "$shot/30.ppm" 64 >"$log.early" 2>&1 || ! pixel-probe "$shot/60.ppm" 64 >"$log.late" 2>&1; then
        record FAIL "pixels: timer" "no frames to read: $(head -1 "$log.early") / $(head -1 "$log.late")"
    elif [ "$(awk '{ print $1 }' "$log.early")" = "$(awk '{ print $1 }' "$log.late")" ]; then
        record FAIL "pixels: timer" "a timer uniform with an initializer gave red $(awk '{ print $1 }' "$log.early") at frames 30 and 60; it is frozen"
    else
        record PASS "pixels: timer" "a timer uniform with an initializer moves: red $(awk '{ print $1 }' "$log.early") at frame 30, $(awk '{ print $1 }' "$log.late") at 60"
    fi

    for name in uniform-written unwritten-is-zero; do
        log="$dir/pixels-$name.log" home="$dir/pixels-$name-home" fx="$dir/pixels-$name-home-fx" shot="$dir/pixels-$name-shots"
        case "$name" in
            uniform-written) chain=uniform_no_initializer want="128 0 0" ;;
            unwritten-is-zero) chain=unknown_source want="0 0 0" ;;
        esac
        fresh_home "$home" "$chain"
        cp "$VKB_FIXTURES/effects/uniform_no_initializer.fx" "$VKB_FIXTURES/effects/unknown_source.fx" "$fx/"
        printf 'uniform_no_initializer = %s\nunknown_source = %s\nuniform_no_initializer.NoInitializer = 0.5\n' \
            "$fx/uniform_no_initializer.fx" "$fx/unknown_source.fx" >>"$home/.config/vkdaedalus/vkdaedalus.conf"
        mkdir -p "$shot"
        run "$log" "$home" "$icd" "$BASALT:$FAULT:$SCREENSHOT:$VALIDATION" 1 1 -- \
            env FAULT_INJECT_FUNCTION=vkMapMemory FAULT_INJECT_POISON=1 FAULT_INJECT_CALLER="$LAYER_LIBRARY" \
            VK_SCREENSHOT_FRAMES=120 VK_SCREENSHOT_DIR="$shot" vkcube --c "$FRAMES" --wsi xcb
        if ! why=$(exited_cleanly "$log"); then
            record FAIL "pixels: $name" "$why"
        elif ! why=$(validation_verdict "$log" "$control"); then
            record FAIL "pixels: $name" "$why"
        elif [ "$(count 'fault_inject: poisoned vkMapMemory called from' "$log")" -lt 1 ]; then
            record FAIL "pixels: $name" "the layer mapped no memory for the poison to fill"
        elif ! pixel-probe "$shot/120.ppm" 64 >"$log.pixels" 2>&1; then
            record FAIL "pixels: $name" "no frame to read: $(head -1 "$log.pixels")"
        elif ! close_to "$log.pixels" "$want"; then
            record FAIL "pixels: $name" "with mapped memory poisoned, frame 120 shows $(head -1 "$log.pixels"), want $want ($chain)"
        else
            record PASS "pixels: $name" "with mapped memory poisoned, $chain presents $want"
        fi
    done
}

frame_variance() {
    local icd="$1" dir="$2" control="$3" name="$4" chain="$5" settings="$6" log home shot why
    log="$dir/variance-$name.log" home="$dir/variance-$name-home" shot="$dir/variance-$name-shots"
    fresh_home "$home" "$chain"
    printf '%b' "$settings" >>"$home/.config/vkdaedalus/vkdaedalus.conf"
    mkdir -p "$shot"
    run "$log" "$home" "$icd" "$BASALT:$SCREENSHOT:$VALIDATION" 1 1 -- \
        env VK_SCREENSHOT_FRAMES=120 VK_SCREENSHOT_DIR="$shot" vkcube --c "$FRAMES" --wsi xcb
    if ! why=$(plain_verdict "$log" "$control"); then
        echo "$name: $why"
        return 1
    fi
    if ! pixel-probe "$shot/120.ppm" 64 >"$log.pixels" 2>&1; then
        echo "$name: no frame to read: $(head -1 "$log.pixels")"
        return 1
    fi
    awk '{ print $5 + $6 + $7 }' "$log.pixels"
}

lane_settings_reach_shaders() {
    local icd="$1" dir="$2" control="$2/overlay-control.log" low="" high=""
    if ! low=$(frame_variance "$icd" "$dir" "$control" cas-soft cas 'casSharpness = 0.0\n') \
        || ! high=$(frame_variance "$icd" "$dir" "$control" cas-sharp cas 'casSharpness = 1.0\n'); then
        record FAIL "settings reach the shader: cas" "$low$high"
    elif [ "$high" -le "$low" ]; then
        record FAIL "settings reach the shader: cas" "casSharpness 0 and 1 give frame variance $low and $high; the setting is ignored"
    else
        record PASS "settings reach the shader: cas" "casSharpness 1 sharpens more than 0 (variance $high against $low)"
    fi
    low="" high=""
    if ! high=$(frame_variance "$icd" "$dir" "$control" dls-raw dls 'dlsSharpness = 1.0\ndlsDenoise = 0.0\n') \
        || ! low=$(frame_variance "$icd" "$dir" "$control" dls-denoised dls 'dlsSharpness = 1.0\ndlsDenoise = 1.0\n'); then
        record FAIL "settings reach the shader: dls" "$high$low"
    elif [ "$low" -ge "$high" ]; then
        record FAIL "settings reach the shader: dls" "dlsDenoise 0 and 1 give frame variance $high and $low; the setting is ignored"
    else
        record PASS "settings reach the shader: dls" "dlsDenoise 1 sharpens less than 0 (variance $low against $high)"
    fi
}

lane_faults() {
    local icd="$1" dir="$2" control="$2/overlay-control.log" call log home why presser injected opened=0 failed=0
    for call in "${FAULT_CALLS[@]}"; do
        log="$dir/fault-$call.log" home="$dir/fault-$call-home"
        fresh_home "$home"
        printf 'overlayKey = %s\n' "$OVERLAY_KEY" >"$home/.config/vkdaedalus/settings.conf"
        background press_overlay_key "$log"
        presser=$!
        run "$log" "$home" "$icd" "$BASALT:$FAULT:$VALIDATION" 1 1 -- \
            env FAULT_INJECT_FUNCTION="$call" FAULT_INJECT_CALLER="$LAYER_LIBRARY" vkcube --c "$FAULT_FRAMES" --wsi xcb
        if wait "$presser" && [ "$(count 'overlay recorded a frame' "$log")" -ge 1 ]; then opened=$((opened + 1)); fi
        injected=$(count "fault_inject: failed $call called from" "$log")
        if ! why=$(exited_cleanly "$log"); then
            record FAIL "fails closed on $call" "$why"
        elif ! why=$(validation_verdict "$log" "$control"); then
            record FAIL "fails closed on $call" "$why"
        elif [ "$injected" -lt 1 ]; then
            record FAIL "fails closed on $call" "the layer never called it"
        elif [ "$(count 'vkDaedalus err:' "$log")" -lt 1 ]; then
            record FAIL "fails closed on $call" "$injected call(s) failed and the layer reported none"
        elif [ -n "${FAULT_REPORTS[$call]:-}" ] && [ "$(count "${FAULT_REPORTS[$call]}" "$log")" -lt 1 ]; then
            record FAIL "fails closed on $call" "$injected call(s) failed without the report '${FAULT_REPORTS[$call]}', so the layer went on without what the call should have given it"
        elif [ "$(awk '/overlay off:/ { off = 1 } /overlay recorded a frame/ { drew = 1 } /Input grabbed for overlay/ { if (off) late = 1; grabbed = 1 } END { print (late || (grabbed && !drew && !off)) ? 1 : 0 }' "$log")" -ne 0 ]; then
            record FAIL "fails closed on $call" "the overlay key grabbed input for an overlay that could not draw"
        else
            continue
        fi
        failed=$((failed + 1))
    done
    if [ "$failed" -eq 0 ]; then
        record PASS "fails closed on every Vulkan failure" "${#FAULT_CALLS[@]} calls each failed from the layer: the game ran on, validation clean, each reported; overlay opened in $opened"
    fi

    fail_once "$icd" "$dir" "$control" vkQueueSubmit 1 acquire 'effect submission failed' "the first frame's effect submit"
    fail_once "$icd" "$dir" "$control" vkQueueSubmit 2 overlay 'overlay submission failed' "the overlay's submit"
    fail_once "$icd" "$dir" "$control" vkWaitForFences 1 start 'a fence wait failed' "the first texture upload's wait"
    fail_once "$icd" "$dir" "$control" vkWaitForFences 1 acquire 'a fence wait failed' "the first frame's wait on the frame before"
    fail_once "$icd" "$dir" "$control" vkAllocateCommandBuffers 1 reload 'recording the effect command buffers failed' \
        "a reload's command buffer recording, for a chain that uploads nothing" cas
    if [ "$(count 'the layer has a queue of its own' "$dir/overlay.log")" -ge 1 ]; then
        fail_once "$icd" "$dir" "$control" vkCreateDevice 1 start 'so it is created again with the queues the game asked for' \
            "a device creation with the layer's own queue, refused below the layer after the chain moved on" "" 1
        made_then_failed "$icd" "$dir" "$control"
    else
        record SKIP "fails once: a device creation with the layer's own queue" "the graphics family has no room for a queue of the layer's own"
    fi
    runs_without_extension "$icd" "$dir" "$control" VK_KHR_image_format_list
    runs_without_extension "$icd" "$dir" "$control" VK_KHR_swapchain_mutable_format
    runs_without_linear_filter "$icd" "$dir" "$control"
}

made_then_failed() {
    local icd="$1" dir="$2" control="$3" log="$2/made-then-failed.log" home="$2/made-then-failed-home" rc failed
    local name="fails once: a device the driver made, failed below the layer (vkCreateDevice)"
    fresh_home "$home"
    run "$log" "$home" "$icd" "$BASALT:$FAULT:$VALIDATION" 1 1 -- \
        env FAULT_INJECT_FUNCTION=vkCreateDevice FAULT_INJECT_NTH=1 FAULT_INJECT_CALLER="$LAYER_LIBRARY" api-probe
    rc=$(cat "$log.rc")
    failed=$(count 'fault_inject: failed vkCreateDevice called from' "$log")
    if [ "$failed" -ne 1 ]; then
        record FAIL "$name" "the fault layer failed $failed device creations, want exactly 1"
    elif [ "$rc" -ne 1 ] || [ "$(count 'api-probe: vkCreateDevice failed' "$log")" -ne 1 ]; then
        record FAIL "$name" "api-probe exit $rc, want 1 after its vkCreateDevice failed: $(tail -1 "$log")"
    elif [ "$(count 'so it is created again' "$log")" -ne 0 ]; then
        record FAIL "$name" "the layer asked the loader again for a device it already counts"
    elif [ "$(count 'vkDaedalus err: .*after the driver had made it' "$log")" -ne 1 ]; then
        record FAIL "$name" "the layer gave the game the error without saying why"
    else
        record PASS "$name" "the layer reported it and gave the game the error without asking again; api-probe saw vkCreateDevice fail and destroyed its instance cleanly"
    fi
}

runs_without_extension() {
    local icd="$1" dir="$2" control="$3" extension="$4" name="runs on a device without $4" log="$2/hide-$4.log" home="$2/hide-$4-home"
    local why submitted
    fresh_home "$home"
    run "$log" "$home" "$icd" "$BASALT:$FAULT:$VALIDATION" 1 1 -- \
        env FAULT_INJECT_HIDE_EXTENSION="$extension" vkcube --c "$FRAMES" --wsi xcb
    submitted=$(count 'trace: the layer submitted a frame' "$log")
    if ! why=$(exited_cleanly "$log"); then
        record FAIL "$name" "$why"
    elif [ "$(count "fault_inject: hid $extension from [1-9]" "$log")" -ne 1 ]; then
        record FAIL "$name" "the fault layer never took $extension out of an extension list, so nothing was tested"
    elif ! why=$(validation_verdict "$log" "$control"); then
        record FAIL "$name" "$why"
    elif [ "$submitted" -lt "$FRAMES" ] || [ "$submitted" -ne "$(count 'trace: present cycle' "$log")" ]; then
        record FAIL "$name" "the layer submitted $submitted frames for $(count 'trace: present cycle' "$log") presents"
    else
        record PASS "$name" "the device was made without it and the layer put all $FRAMES frames through, validation clean"
    fi
}

drive_gpu_reorder() {
    local log="$1" map="$2" profile="$3" ui_log="$1.ui" window card reloads
    : >"$ui_log"
    if ! wait_for_line 'rows from invert_input run on GPU 3 \(' 1 "$log"; then
        ui_result "$ui_log" FAIL "a GPU moved up keeps its rows on its card" "the game never ran invert_input on GPU 3"
        return 0
    fi
    if ! wait_for_line 'present cycle' 30 "$log"; then
        ui_result "$ui_log" FAIL "a GPU moved up keeps its rows on its card" "the game never reached 30 presents"
        return 0
    fi
    card=$(grep -m1 -oE 'rows from invert_input run on GPU 3 \(.*\)' "$log" | sed -E 's/.*GPU 3 \((.*)\)/\1/')
    window=$(DISPLAY="$PRIVATE_DISPLAY" xdotool search --onlyvisible --name '' | tail -1) || true
    ui_take_window "$window"
    if ! hold_key_until "$OVERLAY_KEY" 'overlay opened' 1 "$log"; then
        ui_result "$ui_log" FAIL "a GPU moved up keeps its rows on its card" "$OVERLAY_KEY was held but the overlay never opened"
        return 0
    fi
    ui_click "$map" "GPUs" 1 "$log" || true
    ui_wait "$map" "Up##3" "" "" "$log" || true
    reloads=$(count 'hot-reloading config and effects' "$log")
    ui_click "$map" "Up##3" 1 "$log" || true
    if wait_for_count 'hot-reloading config and effects' $((reloads + 1)) "$log" && file_holds "$log" "rows from invert_input run on GPU 2 ($card)"; then
        ui_result "$ui_log" PASS "a GPU moved up keeps its rows on its card" "after Up on GPU 3, invert_input ran on GPU 2, the same card"
    else
        ui_result "$ui_log" FAIL "a GPU moved up keeps its rows on its card" "after Up on GPU 3, invert_input never ran on the same card as GPU 2: $(grep -E 'run on GPU|stay on the game|move to the game' "$log" | tail -1)"
    fi
    reloads=$(count 'hot-reloading config and effects' "$log")
    ui_click "$map" "Remove##2" 1 "$log" || true
    if wait_for_count 'hot-reloading config and effects' $((reloads + 1)) "$log" && file_says "$profile" '^invert_input\.gpu = 1$'; then
        ui_result "$ui_log" PASS "a removed GPU's rows go to GPU 1" "Remove on GPU 2 saved invert_input.gpu = 1"
    else
        ui_result "$ui_log" FAIL "a removed GPU's rows go to GPU 1" "Remove on GPU 2 never saved invert_input.gpu = 1: $(grep -E '^invert_input' "$profile" | tr '\n' ' ')"
    fi
    hold_key_until "$OVERLAY_KEY" 'overlay closed' 1 "$log" || true
}

drive_late_gpu() {
    local log="$1" profile="$2" other="$3" reloads
    if ! wait_for_line 'present cycle' 30 "$log"; then return 0; fi
    reloads=$(count 'hot-reloading config and effects' "$log")
    printf 'gpus = %s\nsmaa.gpu = 2\n' "$other" >>"$profile"
    wait_for_count 'hot-reloading config and effects' $((reloads + 1)) "$log" || true
    wait_for_line 'shares nothing until the game restarts' 1 "$log" || true
}

drive_first_move() {
    local log="$1" map="$2" ui_log="$1.ui" window label runs='rows from invert_input run on GPU 2 \(' reloads name="a row first sent to GPU 2 live runs there"
    : >"$ui_log"
    if ! wait_for_line 'present cycle' 30 "$log" || [ "$(count "$runs" "$log")" -ne 0 ]; then
        ui_result "$ui_log" FAIL "$name" "the game never presented, or invert_input ran on GPU 2 before it was sent there"
        return 0
    fi
    window=$(DISPLAY="$PRIVATE_DISPLAY" xdotool search --onlyvisible --name '' | tail -1) || true
    ui_take_window "$window"
    if ! hold_key_until "$OVERLAY_KEY" 'overlay opened' 1 "$log"; then
        ui_result "$ui_log" FAIL "$name" "$OVERLAY_KEY was held but the overlay never opened"
        return 0
    fi
    reloads=$(count 'hot-reloading config and effects' "$log")
    ui_click "$map" "invert_input" 3 "$log" || true
    ui_hover "$map" "GPU" "$log" || true
    label=$(ui_label_starting "$map" "GPU 2: " "$log") || true
    if [ -z "$label" ] || ! ui_click "$map" "$label" 1 "$log"; then
        ui_result "$ui_log" FAIL "$name" "the row menu offered no GPU 2"
        return 0
    fi
    if ! wait_for_count 'hot-reloading config and effects' $((reloads + 1)) "$log"; then
        ui_result "$ui_log" FAIL "$name" "choosing GPU 2 never rebuilt the chain"
        return 0
    fi
    sleep 1
    if [ "$(count "$runs" "$log")" -ne 1 ]; then
        ui_result "$ui_log" FAIL "$name" "after choosing GPU 2 the layer said: $(grep -E 'stay on the game|run on GPU 2' "$log" | tail -1)"
        return 0
    fi
    ui_result "$ui_log" PASS "$name" "with GPU 2 listed and every row on GPU 1 at the start, the row menu sent invert_input to GPU 2 and it ran there at once"
}

drive_gpu_switch() {
    local log="$1" map="$2" ui_log="$1.ui" window label runs='rows from invert_input run on GPU 2 \(' reloads target
    : >"$ui_log"
    if ! wait_for_line 'present cycle' 30 "$log" || [ "$(count "$runs" "$log")" -ne 1 ]; then
        ui_result "$ui_log" FAIL "a row moves between GPUs live" "the game never ran with invert_input on GPU 2"
        return 0
    fi
    window=$(DISPLAY="$PRIVATE_DISPLAY" xdotool search --onlyvisible --name '' | tail -1) || true
    ui_take_window "$window"
    if ! hold_key_until "$OVERLAY_KEY" 'overlay opened' 1 "$log"; then
        ui_result "$ui_log" FAIL "a row moves between GPUs live" "$OVERLAY_KEY was held but the overlay never opened"
        return 0
    fi
    for target in 1 2; do
        reloads=$(count 'hot-reloading config and effects' "$log")
        ui_click "$map" "invert_input" 3 "$log" || true
        ui_hover "$map" "GPU" "$log" || true
        label=$(ui_label_starting "$map" "GPU $target: " "$log") || true
        if [ "$target" = 1 ] && [ -n "$label" ]; then ui_state "$map" gpus-01-gpu-menu "$ui_log" "an effect's GPU submenu"; fi
        if [ -z "$label" ] || ! ui_click "$map" "$label" 1 "$log"; then
            ui_result "$ui_log" FAIL "a row moves between GPUs live" "the row menu offered no GPU $target"
            return 0
        fi
        if ! wait_for_count 'hot-reloading config and effects' $((reloads + 1)) "$log" || ! file_says "$log" 'reloading [0-9]+ effects'; then
            ui_result "$ui_log" FAIL "a row moves between GPUs live" "choosing GPU $target never rebuilt the chain"
            return 0
        fi
        sleep 1
        if [ "$(count "$runs" "$log")" -ne "$target" ]; then
            ui_result "$ui_log" FAIL "a row moves between GPUs live" "after choosing GPU $target the layer said invert_input runs on GPU 2 $(count "$runs" "$log") times"
            return 0
        fi
    done
    ui_click "$map" "GPUs" 1 "$log" || true
    ui_state "$map" gpus-02-gpus "$ui_log" "the GPUs card after the moves"
    reloads=$(count 'hot-reloading config and effects' "$log")
    ui_click "$map" "Performance" 1 "$log" || true
    if ! ui_wait "$map" "Per-effect GPU timing" "" "" "$log" || ! ui_click "$map" "Per-effect GPU timing" 1 "$log" || ! wait_for_count 'hot-reloading config and effects' $((reloads + 1)) "$log"; then
        ui_result "$ui_log" FAIL "the cards fit a timed chain on two GPUs" "Per-effect GPU timing never rebuilt the chain"
    else
        sleep 1
        ui_state "$map" perf-01-timed-offload "$ui_log" "the Performance card timing a chain that crosses to GPU 2"
        ui_in_view "$map" "##performancebody" "the Performance card timing a chain on two GPUs at 1280x720" "$ui_log" "##frameintervals|Per-effect GPU timing"
        ui_click "$map" "GPUs" 1 "$log" || true
        ui_state "$map" gpus-06-timed "$ui_log" "the GPUs card with GPU 2's copies timed"
        ui_in_view "$map" "##gpusbody" "the GPUs card timing a chain on two GPUs at 1280x720" "$ui_log" "##gpuname"
    fi
    ui_states_verdict "$ui_log"
    ui_result "$ui_log" PASS "a row moves between GPUs live" "the row menu moved invert_input to GPU 1 and back to GPU 2, each rebuilding the chain while the game ran"
}

ui_label_starting() {
    local map="$1" prefix="$2" log="$3" tries=0 found
    while true; do
        found=$(awk -F'\t' -v p="$prefix" 'NR > 1 && index($2, p) == 1 { print $2; exit }' "$map" 2>/dev/null)
        if [ -n "$found" ]; then
            printf '%s\n' "$found"
            return 0
        fi
        if [ -f "$log.rc" ] || [ "$tries" -ge 100 ]; then return 1; fi
        tries=$((tries + 1))
        sleep 0.1
    done
}

wait_for_count() {
    local pattern="$1" want="$2" log="$3" tries=0
    until [ "$(count "$pattern" "$log")" -ge "$want" ]; do
        if [ -f "$log.rc" ] || [ "$tries" -ge 100 ]; then return 1; fi
        tries=$((tries + 1))
        sleep 0.1
    done
}

offload_chain_case() {
    local dir="$1" every="$2" control="$3" name="$4" gpus="$5" rows="$6" expected="$7" mode="$8" want="64 128 191"
    local log="$dir/offload-$mode.log" home="$dir/offload-$mode-home" shot="$dir/offload-$mode-shots" wanted unsaid=""
    fresh_home "$home" solid_fill:invert_input:invert_again
    cp "$VKB_FIXTURES/effects/solid_fill.fx" "$VKB_FIXTURES/effects/invert_input.fx" "$home-fx/"
    printf 'solid_fill = %s\ninvert_input = %s\ninvert_again = %s\ngpus = %s\n' "$home-fx/solid_fill.fx" "$home-fx/invert_input.fx" \
        "$home-fx/invert_input.fx" "$gpus" >>"$home/.config/vkdaedalus/vkdaedalus.conf"
    printf '%b\n' "$rows" >>"$home/.config/vkdaedalus/vkdaedalus.conf"
    rm -rf "$shot"
    mkdir -p "$shot"
    run "$log" "$home" "$every" "$BASALT:$SCREENSHOT:$VALIDATION" 1 1 -- \
        env VK_SCREENSHOT_FRAMES=120 VK_SCREENSHOT_DIR="$shot" vkcube --c "$FRAMES" --wsi xcb
    pixel-probe "$shot/120.ppm" 64 >"$log.pixels" 2>&1
    while IFS= read -r wanted; do
        [ "$(count "vkDaedalus info: +$wanted" "$log")" -ge 1 ] || unsaid="$wanted"
    done <<<"$expected"
    if ! why=$(plain_verdict "$log" "$control"); then
        record FAIL "$name" "$why"
    elif ! grep -qE 'frame statistics: .*, 0 round trip reads, ' "$log"; then
        record FAIL "$name" "the overlay never opened, yet the round trip was read back: $(grep -m1 -oE '[0-9]+ round trip reads' "$log" || echo no frame statistics line)"
    elif [ -n "$unsaid" ] || [ "$(count 'stay on the game.s GPU|move to the game.s GPU' "$log")" -ne 0 ]; then
        record FAIL "$name" "the layer never said $unsaid: $(grep -m1 -E 'stay on the game|move to the game|run on GPU|made a device on' "$log" || echo nothing)"
    elif ! close_to "$log.pixels" "$want"; then
        record FAIL "$name" "frame 120 shows $(head -1 "$log.pixels"), want $want (solid_fill, invert_input, invert_input again)"
    else
        record PASS "$name" "$(tr '\n' ';' <<<"$expected" | sed 's/;$//; s/;/; /g'): frame 120 shows $want, validation clean"
    fi
}

lane_offload() {
    local icd="$1" dir="$2" every without="$2/offload-choice-without.log" with="$2/offload-choice-with.log"
    local assigned="$2/offload-choice-assigned.log" helper="$2/offload-choice-helper.log" why mode listed other made absent
    local name="the game's GPU list and pick stay the game's" sharing="sharing extensions follow the profile"
    local -a DEVICE_SELECT=()
    local enabled='vkDaedalus info: +the game'"'"'s device on .* enables the sharing extensions, as this profile lists another GPU'
    local refused='vkDaedalus warn: +this profile lists another GPU, but .* (lacks VK_[A-Za-z_]+|offers Vulkan below 1.1), so every effect runs on it'
    every=$(IFS=:; echo "${icds[*]}")
    for mode in without with assigned helper; do
        fresh_home "$dir/offload-choice-$mode-home"
    done
    printf 'gpus = ffff:ffff\ncas.gpu = 2\n' >>"$dir/offload-choice-assigned-home/.config/vkdaedalus/vkdaedalus.conf"
    run "$without" "$dir/offload-choice-without-home" "$every" "$VALIDATION" 0 1 -- gpu-choice-probe 60 "$LANE_GPU_ID"
    run "$with" "$dir/offload-choice-with-home" "$every" "$BASALT:$VALIDATION" 1 1 -- gpu-choice-probe 60 "$LANE_GPU_ID"
    run "$assigned" "$dir/offload-choice-assigned-home" "$every" "$BASALT:$VALIDATION" 1 1 -- gpu-choice-probe 60 "$LANE_GPU_ID"
    listed='^gpu-choice-probe: (before|after) device|^gpu-choice-probe: default pick'
    other=$(awk '/^gpu-choice-probe: default pick/ { game = $NF } /^gpu-choice-probe: before device/ { n++; id[n] = $5; type[n] = $7 } END { for (i = 1; i <= n; i++) if (type[i] != "4" && id[i] != game) { print id[i]; exit } }' "$without.stdout")
    if [ -n "$other" ]; then
        printf 'gpus = %s\ncas.gpu = 2\n' "$other" >>"$dir/offload-choice-helper-home/.config/vkdaedalus/vkdaedalus.conf"
        run "$helper" "$dir/offload-choice-helper-home" "$every" "$BASALT:$VALIDATION" 1 1 -- gpu-choice-probe 60 "$LANE_GPU_ID"
    fi
    if ! why=$(exited_cleanly "$without"); then
        record FAIL "$name" "the probe fails without the layer: $why"
    elif ! why=$(plain_verdict "$with" "$without"); then
        record FAIL "$name" "$why"
    elif ! why=$(plain_verdict "$assigned" "$without"); then
        record FAIL "$name" "with a profile that assigns GPU 2: $why"
    elif ! diff <(grep -E "$listed" "$without.stdout") <(grep -E "$listed" "$with.stdout") >"$with.diff"; then
        record FAIL "$name" "with the layer the game sees another list or pick: $(grep -E '^[<>]' "$with.diff" | head -2 | tr '\n' ' ')"
    elif ! diff <(grep -E "$listed" "$without.stdout") <(grep -E "$listed" "$assigned.stdout") >"$assigned.diff"; then
        record FAIL "$name" "with a profile that assigns GPU 2 the game sees another list or pick: $(grep -E '^[<>]' "$assigned.diff" | head -2 | tr '\n' ' ')"
    elif [ "$(grep -c '^gpu-choice-probe: before device' "$with.stdout")" -lt 1 ]; then
        record FAIL "$name" "the probe listed no device, so nothing was compared"
    else
        record PASS "$name" "$(grep -c '^gpu-choice-probe: before device' "$with.stdout") GPUs from every driver, in the same order with the same default pick, before and after 60 presents, with the layer as without, with and without a GPU 2 assigned"
    fi
    if [ "$(count 'sharing extensions|run on another GPU' "$with")" -ne 0 ]; then
        record FAIL "$sharing" "a profile with every row on the game's GPU touched the sharing extensions: $(grep -m1 -E 'sharing extensions|run on another GPU' "$with")"
    elif [ "$(count "$enabled" "$assigned")" -ne 1 ] && [ "$(count "$refused" "$assigned")" -ne 1 ]; then
        record FAIL "$sharing" "a profile assigning GPU 2 neither enabled the sharing extensions nor named what the device lacks"
    else
        record PASS "$sharing" "absent with every row on the game's GPU; with cas on GPU 2: $(grep -m1 -E "$enabled|$refused" "$assigned" | sed -E 's/^vkDaedalus (info|warn): +//')"
    fi
    absent='vkDaedalus warn: +rows from cas stay on the game'"'"'s GPU: GPU 2 \(ffff:ffff\) is not in this Vulkan instance'
    if [ "$(count "$refused" "$assigned")" -eq 1 ]; then
        record SKIP "an absent GPU 2 is refused once" "the game's device cannot share, so no GPU 2 is looked for"
    elif [ "$(count "$absent" "$assigned")" -ne 1 ] || [ "$(count 'made a device on' "$assigned")" -ne 0 ]; then
        record FAIL "an absent GPU 2 is refused once" "with gpus = ffff:ffff the layer said: $(grep -m1 -E 'stay on the game|made a device on' "$assigned" || echo nothing)"
    else
        record PASS "an absent GPU 2 is refused once" "$(grep -m1 -E "$absent" "$assigned" | sed -E 's/^vkDaedalus warn: +//'), across the swapchains of the run"
    fi
    made='vkDaedalus info: +made a device on GPU 2 \('"$other"', .*\) for rows from cas'
    if [ -z "$other" ]; then
        record SKIP "a device on GPU 2 is made and closed" "this machine has no second GPU in the instance"
    elif ! why=$(plain_verdict "$helper" "$without"); then
        record FAIL "a device on GPU 2 is made and closed" "with GPU 2 = $other: $why"
    elif [ "$(count "$made" "$helper")" -ne 1 ]; then
        record FAIL "a device on GPU 2 is made and closed" "with GPU 2 = $other the layer said: $(grep -m1 -E 'stay on the game|made a device on|sharing extensions' "$helper" || echo nothing)"
    elif ! diff <(grep -E "$listed" "$without.stdout") <(grep -E "$listed" "$helper.stdout") >"$helper.diff"; then
        record FAIL "a device on GPU 2 is made and closed" "with a device on GPU 2 the game sees another list or pick: $(grep -E '^[<>]' "$helper.diff" | head -2 | tr '\n' ' ')"
    else
        record PASS "a device on GPU 2 is made and closed" "$(grep -m1 -E "$made" "$helper" | sed -E 's/^vkDaedalus info: +//'), once, through the loader's callback, validation clean to its destroy, the game's list and pick unchanged"
    fi
    local LSFG_DIR="$dir/real-lsfg-offload" lsfgName="lsfg-vk frames with rows on GPU 2"
    local lsfgControl="$dir/offload-lsfg-control.log" lsfgLog="$dir/offload-lsfg.log"
    if [ -z "$other" ]; then
        record SKIP "$lsfgName" "this machine has no second GPU in the instance"
    elif ! lsfg_frames_ready "$LSFG_DIR"; then
        record SKIP "$lsfgName" "$(lsfg_frames_skip)"
    else
        run "$lsfgControl" "$dir/offload-lsfg-control-home" "$every" "$LSFG:$VALIDATION" 0 1 -- gpu-choice-probe "$FRAMES" "$LANE_GPU_ID"
        if ! why=$(lsfg_ran "$lsfgControl"); then
            record FAIL "$lsfgName" "lsfg-vk alone: $why"
        else
            local order layers want home
            for order in before after; do
                if [ "$order" = before ]; then
                    layers="$BASALT:$LSFG:$VALIDATION" want=1
                else
                    layers="$LSFG:$BASALT:$VALIDATION" want=2
                fi
                lsfgLog="$dir/offload-lsfg-$order.log" home="$dir/offload-lsfg-$order-home"
                fresh_home "$home" cas:smaa
                printf 'gpus = %s\nsmaa.gpu = 2\n' "$other" >>"$home/.config/vkdaedalus/vkdaedalus.conf"
                run "$lsfgLog" "$home" "$every" "$layers" 1 1 -- gpu-choice-probe "$FRAMES" "$LANE_GPU_ID"
                if ! why=$(lsfg_frames_verdict "$lsfgLog" "$lsfgControl" "$FRAMES" "$want"); then
                    record FAIL "$lsfgName, effects $order" "$why"
                elif [ "$(count 'vkDaedalus info: +rows from smaa run on GPU 2 \(' "$lsfgLog")" -lt 1 ]; then
                    record FAIL "$lsfgName, effects $order" "smaa never ran on GPU 2: $(grep -m1 -E 'stay on the game|move to the game|made a device on' "$lsfgLog" || echo nothing was said)"
                elif [ "$(count 'rows from cas run on GPU' "$lsfgLog")" -ne 0 ]; then
                    record FAIL "$lsfgName, effects $order" "cas left GPU 1: $(grep -m1 'rows from cas run on GPU' "$lsfgLog")"
                else
                    record PASS "$lsfgName, effects $order" "cas on GPU 1 and smaa on GPU $other for $why presents of $FRAMES game frames, lsfg-vk at 2x on the game's GPU $order the layer's effects, validation clean against lsfg-vk alone"
                fi
            done
        fi
    fi
    local crossing="the frame crosses to GPU 2 and comes back" mode chain=solid_fill:invert_input want="191 127 64" control="$2/offload-pixels-control.log"
    if [ -z "$other" ]; then
        record SKIP "$crossing" "this machine has no second GPU in the instance"
        return
    fi
    local -a pinned=("VK_LOADER_DEVICE_ID_FILTER=${LANE_GPU_ID#*:}")
    for mode in control on-game on-helper; do
        local log="$dir/offload-pixels-$mode.log" home="$dir/offload-pixels-$mode-home" shot="$dir/offload-pixels-$mode-shots"
        fresh_home "$home" "$chain"
        cp "$VKB_FIXTURES/effects/solid_fill.fx" "$VKB_FIXTURES/effects/invert_input.fx" "$home-fx/"
        printf 'solid_fill = %s\ninvert_input = %s\n' "$home-fx/solid_fill.fx" "$home-fx/invert_input.fx" >>"$home/.config/vkdaedalus/vkdaedalus.conf"
        if [ "$mode" = on-helper ]; then
            printf 'gpus = %s\ninvert_input.gpu = 2\n' "$other" >>"$home/.config/vkdaedalus/vkdaedalus.conf"
        fi
        rm -rf "$shot"
        mkdir -p "$shot"
        DEVICE_SELECT=("${pinned[@]}")
        if [ "$mode" = control ]; then
            run "$log" "$home" "$every" "$VALIDATION" 0 1 -- vkcube --c "$FRAMES" --wsi xcb
        else
            run "$log" "$home" "$every" "$BASALT:$SCREENSHOT:$VALIDATION" 1 1 -- \
                env VK_SCREENSHOT_FRAMES=120 VK_SCREENSHOT_DIR="$shot" vkcube --c "$FRAMES" --wsi xcb
            pixel-probe "$shot/120.ppm" 64 >"$log.pixels" 2>&1
        fi
        DEVICE_SELECT=()
    done
    local game="$dir/offload-pixels-on-game.log" moved="$dir/offload-pixels-on-helper.log"
    local runs='vkDaedalus info: +rows from invert_input run on GPU 2 \('
    local away='stay on the game.s GPU|move to the game.s GPU'
    if ! why=$(plain_verdict "$game" "$control"); then
        record FAIL "$crossing" "on the game's GPU alone: $why"
    elif ! why=$(plain_verdict "$moved" "$control"); then
        record FAIL "$crossing" "with invert_input on GPU 2 = $other: $why"
    elif [ "$(count "$away" "$moved")" -ne 0 ] || [ "$(count "$runs" "$moved")" -lt 1 ]; then
        record FAIL "$crossing" "with invert_input on GPU 2 the layer said: $(grep -m1 -E "$away|run on GPU 2|made a device on|sharing extensions" "$moved" || echo nothing)"
    elif ! close_to "$game.pixels" "$want"; then
        record FAIL "$crossing" "on the game's GPU alone frame 120 shows $(head -1 "$game.pixels"), want $want ($chain)"
    elif ! close_to "$moved.pixels" "$want"; then
        record FAIL "$crossing" "with invert_input on GPU 2 frame 120 shows $(head -1 "$moved.pixels"), want $want, as on the game's GPU"
    else
        local same="byte for byte"
        cmp -s "$dir/offload-pixels-on-game-shots/120.ppm" "$dir/offload-pixels-on-helper-shots/120.ppm" || same="within 3 per channel, not byte for byte"
        record PASS "$crossing" "$(grep -m1 -E "$runs" "$moved" | sed -E 's/^vkDaedalus info: +//'): frame 120 shows $want as on the game's GPU alone, $same, validation clean"
    fi
    local stalled="$dir/offload-stalled.log" stalledHome="$dir/offload-stalled-home" stalledName="a failed wait for GPU 2 keeps its rows on GPU 1 until a reload"
    fresh_home "$stalledHome" "$chain"
    cp "$VKB_FIXTURES/effects/solid_fill.fx" "$VKB_FIXTURES/effects/invert_input.fx" "$stalledHome-fx/"
    printf 'solid_fill = %s\ninvert_input = %s\ngpus = %s\ninvert_input.gpu = 2\n' "$stalledHome-fx/solid_fill.fx" "$stalledHome-fx/invert_input.fx" "$other" \
        >>"$stalledHome/.config/vkdaedalus/vkdaedalus.conf"
    DEVICE_SELECT=("${pinned[@]}")
    run "$stalled" "$stalledHome" "$every" "$BASALT:$FAULT:$VALIDATION" 1 1 -- env FAULT_INJECT_FUNCTION=vkWaitForFences FAULT_INJECT_DEVICE=2 \
        FAULT_INJECT_FROM=acquire FAULT_INJECT_NTH=10 FAULT_INJECT_CALLER="$LAYER_LIBRARY" vkcube --c "$FRAMES" --wsi xcb
    DEVICE_SELECT=()
    if ! why=$(exited_cleanly "$stalled") || ! why=$(validation_verdict "$stalled" "$control"); then
        record FAIL "$stalledName" "$why"
    elif [ "$(count 'fault_inject: failed vkWaitForFences called from' "$stalled")" -ne 1 ]; then
        record FAIL "$stalledName" "the fault layer failed $(count 'fault_inject: failed vkWaitForFences called from' "$stalled") waits on GPU 2's device, want 1"
    elif [ "$(count 'rows from invert_input run on GPU 2 \(' "$stalled")" -ne 1 ] || [ "$(count 'move to the game.s GPU until a reload' "$stalled")" -lt 1 ]; then
        record FAIL "$stalledName" "after one failed wait, invert_input was put on GPU 2 $(count 'rows from invert_input run on GPU 2 \(' "$stalled") times, want once, and the layer said: $(grep -m1 -E 'previous frame' "$stalled" || echo nothing)"
    else
        record PASS "$stalledName" "one failed wait on GPU 2's device moved invert_input to GPU 1 for the rest of the run, validation clean"
    fi
    local switched="$dir/offload-switch.log" switchHome="$dir/offload-switch-home" map="$dir/offload-switch.map" driver verdict caseName detail
    fresh_home "$switchHome" "$chain"
    printf 'overlayKey = %s\n' "$OVERLAY_KEY" >"$switchHome/.config/vkdaedalus/settings.conf"
    cp "$VKB_FIXTURES/effects/solid_fill.fx" "$VKB_FIXTURES/effects/invert_input.fx" "$switchHome-fx/"
    printf 'solid_fill = %s\ninvert_input = %s\ngpus = %s\ninvert_input.gpu = 2\n' "$switchHome-fx/solid_fill.fx" "$switchHome-fx/invert_input.fx" "$other" \
        >>"$switchHome/.config/vkdaedalus/vkdaedalus.conf"
    rm -f "$map"
    DEVICE_SELECT=("${pinned[@]}")
    background drive_gpu_switch "$switched" "$map"
    driver=$!
    run "$switched" "$switchHome" "$every" "$BASALT:$VALIDATION" 1 1 -- env VKDAEDALUS_UI_MAP="$map" vkcube --c "$UI_FRAMES" --wsi xcb --width 1280 --height 720
    driver_finished "drive_gpu_switch"
    DEVICE_SELECT=()
    while IFS=$'\t' read -r verdict caseName detail; do
        record "$verdict" "$caseName" "$detail"
    done <"$switched.ui"
    if ! why=$(plain_verdict "$switched" "$control"); then
        record FAIL "a row moves between GPUs live, cleanly" "$why"
    fi
    if grep -qE 'frame statistics: .*, [1-9][0-9]* round trip reads, ' "$switched"; then
        record PASS "the round trip is read while a card shows it" "$(grep -oE '[0-9]+ round trip reads' "$switched" | tail -1) with the Performance and GPUs cards open"
    else
        record FAIL "the round trip is read while a card shows it" "with the Performance and GPUs cards open: $(grep -m1 -oE '[0-9]+ round trip reads' "$switched" || echo no frame statistics line)"
    fi
    local szSpec szPercent szTag szDisplay szLog szHome szMap
    for szSpec in "84|1080p|1080p at 125% (84% at 1280x720)" "75|1440p|1440p at 150% (75% at 1280x720)"; do
        IFS='|' read -r szPercent szTag szDisplay <<<"$szSpec"
        szLog="$dir/offload-sizes-$szTag.log" szHome="$dir/offload-sizes-$szTag-home" szMap="$dir/offload-sizes-$szTag.map"
        fresh_home "$szHome" "$chain"
        printf 'overlayKey = %s\noverlaySize = %s\neffectGpuTiming = true\n' "$OVERLAY_KEY" "$szPercent" >"$szHome/.config/vkdaedalus/settings.conf"
        cp "$VKB_FIXTURES/effects/solid_fill.fx" "$VKB_FIXTURES/effects/invert_input.fx" "$szHome-fx/"
        printf 'solid_fill = %s\ninvert_input = %s\ngpus = %s\ninvert_input.gpu = 2\n' "$szHome-fx/solid_fill.fx" "$szHome-fx/invert_input.fx" "$other" \
            >>"$szHome/.config/vkdaedalus/vkdaedalus.conf"
        rm -f "$szMap"
        DEVICE_SELECT=("${pinned[@]}")
        background drive_card_sizes "$szLog" "$szMap" "$szTag" "$szDisplay, timing a chain on two GPUs" -timed Performance GPUs
        driver=$!
        run "$szLog" "$szHome" "$every" "$BASALT:$VALIDATION" 1 1 -- env VKDAEDALUS_UI_MAP="$szMap" vkcube --c "$UI_FRAMES" --wsi xcb --width 1280 --height 720
        driver_finished "drive_card_sizes"
        DEVICE_SELECT=()
        while IFS=$'\t' read -r verdict caseName detail; do
            record "$verdict" "$caseName" "$detail"
        done <"$szLog.ui"
        if ! why=$(plain_verdict "$szLog" "$control"); then
            record FAIL "the cards at $szDisplay timing a chain on two GPUs, cleanly" "$why"
        fi
    done
    local firstMove="$dir/offload-first-move.log" firstHome="$dir/offload-first-move-home" firstMap="$dir/offload-first-move.map"
    fresh_home "$firstHome" "$chain"
    printf 'overlayKey = %s\n' "$OVERLAY_KEY" >"$firstHome/.config/vkdaedalus/settings.conf"
    cp "$VKB_FIXTURES/effects/solid_fill.fx" "$VKB_FIXTURES/effects/invert_input.fx" "$firstHome-fx/"
    printf 'solid_fill = %s\ninvert_input = %s\ngpus = %s\n' "$firstHome-fx/solid_fill.fx" "$firstHome-fx/invert_input.fx" "$other" \
        >>"$firstHome/.config/vkdaedalus/vkdaedalus.conf"
    rm -f "$firstMap"
    DEVICE_SELECT=("${pinned[@]}")
    background drive_first_move "$firstMove" "$firstMap"
    driver=$!
    run "$firstMove" "$firstHome" "$every" "$BASALT:$VALIDATION" 1 1 -- env VKDAEDALUS_UI_MAP="$firstMap" vkcube --c "$UI_FRAMES" --wsi xcb --width 1280 --height 720
    driver_finished "drive_first_move"
    DEVICE_SELECT=()
    while IFS=$'\t' read -r verdict caseName detail; do
        record "$verdict" "$caseName" "$detail"
    done <"$firstMove.ui"
    local late="$dir/offload-late-gpu.log" lateNext="$dir/offload-late-gpu-next.log" lateHome="$dir/offload-late-gpu-home"
    local lateName="a GPU listed mid-game is used from the next start" lateRuns='vkDaedalus info: +rows from smaa run on GPU 2 \('
    fresh_home "$lateHome" cas:smaa
    mkdir -p "$lateHome/.config/vkdaedalus/configs" "$lateHome/.config/vkdaedalus/profiles"
    printf 'effects = cas:smaa\n' >"$lateHome/.config/vkdaedalus/profiles/default.conf"
    printf 'safeAntiCheat = true\n' >"$lateHome/.config/vkdaedalus/configs/vkcube.conf"
    DEVICE_SELECT=("${pinned[@]}")
    background drive_late_gpu "$late" "$lateHome/.config/vkdaedalus/profiles/default.conf" "$other"
    driver=$!
    run "$late" "$lateHome" "$every" "$BASALT:$VALIDATION" 1 1 -- vkcube --c "$UI_FRAMES" --wsi xcb
    driver_finished "drive_late_gpu"
    run "$lateNext" "$lateHome" "$every" "$BASALT:$VALIDATION" 1 1 -- vkcube --c "$FRAMES" --wsi xcb
    DEVICE_SELECT=()
    if ! why=$(plain_verdict "$late" "$control"); then
        record FAIL "$lateName" "the run it was listed in: $why"
    elif ! why=$(plain_verdict "$lateNext" "$control"); then
        record FAIL "$lateName" "the next start: $why"
    elif [ "$(count 'shares nothing until the game restarts' "$late")" -lt 1 ] || [ "$(count "$lateRuns" "$late")" -ne 0 ]; then
        record FAIL "$lateName" "listed while vkcube ran, the layer said: $(grep -m1 -E 'stay on the game|run on GPU 2' "$late" || echo nothing)"
    elif [ "$(count "$lateRuns" "$lateNext")" -lt 1 ]; then
        record FAIL "$lateName" "at the next start smaa never ran on GPU 2: $(grep -m1 -E 'stay on the game|move to the game|made a device on' "$lateNext" || echo nothing was said)"
    else
        record PASS "$lateName" "listed in vkcube's profile while it ran, smaa stayed on GPU 1 and the layer said the device shares nothing until a restart; the next start ran smaa on GPU $other"
    fi
    local others third
    others=$(awk '/^gpu-choice-probe: default pick/ { game = $NF } /^gpu-choice-probe: before device/ { n++; id[n] = $5; type[n] = $7 } END { for (i = 1; i <= n; i++) if (type[i] != "4" && id[i] != game) print id[i] }' "$without.stdout")
    third=$(sed -n 2p <<<"$others")
    DEVICE_SELECT=("${pinned[@]}")
    offload_chain_case "$dir" "$every" "$control" "the chain returns to GPU 1 mid-chain" "$other" 'invert_input.gpu = 2\ninvert_again.gpu = 1' \
        'rows from invert_input run on GPU 2 \(' chain-back
    if [ -n "$third" ]; then
        offload_chain_case "$dir" "$every" "$control" "the chain crosses GPU 2 then GPU 3" "$other,$third" 'invert_input.gpu = 2\ninvert_again.gpu = 3' \
            "$(printf '%s\n%s' 'rows from invert_input run on GPU 2 \(' 'rows from invert_again run on GPU 3 \(')" chain-three
        local szThree
        for szThree in "0|720p|1280x720" "84|1080p|1080p at 125% (84% at 1280x720)" "75|1440p|1440p at 150% (75% at 1280x720)"; do
            IFS='|' read -r szPercent szTag szDisplay <<<"$szThree"
            szLog="$dir/offload-three-$szTag.log" szHome="$dir/offload-three-$szTag-home" szMap="$dir/offload-three-$szTag.map"
            fresh_home "$szHome" solid_fill:invert_input:invert_again
            printf 'overlayKey = %s\noverlaySize = %s\neffectGpuTiming = true\n' "$OVERLAY_KEY" "$szPercent" >"$szHome/.config/vkdaedalus/settings.conf"
            cp "$VKB_FIXTURES/effects/solid_fill.fx" "$VKB_FIXTURES/effects/invert_input.fx" "$szHome-fx/"
            printf 'solid_fill = %s\ninvert_input = %s\ninvert_again = %s\ngpus = %s\ninvert_input.gpu = 2\ninvert_again.gpu = 3\n' "$szHome-fx/solid_fill.fx" \
                "$szHome-fx/invert_input.fx" "$szHome-fx/invert_input.fx" "$other,$third" >>"$szHome/.config/vkdaedalus/vkdaedalus.conf"
            rm -f "$szMap"
            background drive_card_sizes "$szLog" "$szMap" "$szTag" "$szDisplay, the chain on GPUs 2 and 3" -three Performance GPUs
            driver=$!
            run "$szLog" "$szHome" "$every" "$BASALT:$VALIDATION" 1 1 -- env VKDAEDALUS_UI_MAP="$szMap" vkcube --c "$UI_FRAMES" --wsi xcb --width 1280 --height 720
            driver_finished "drive_card_sizes"
            while IFS=$'\t' read -r verdict caseName detail; do
                record "$verdict" "$caseName" "$detail"
            done <"$szLog.ui"
            if ! why=$(plain_verdict "$szLog" "$control"); then
                record FAIL "the cards at $szDisplay with the chain on GPUs 2 and 3, cleanly" "$why"
            fi
        done
        local reorder="$dir/offload-reorder.log" reorderHome="$dir/offload-reorder-home" reorderMap="$dir/offload-reorder.map"
        fresh_home "$reorderHome" solid_fill:invert_input
        printf 'overlayKey = %s\n' "$OVERLAY_KEY" >"$reorderHome/.config/vkdaedalus/settings.conf"
        cp "$VKB_FIXTURES/effects/solid_fill.fx" "$VKB_FIXTURES/effects/invert_input.fx" "$reorderHome-fx/"
        printf 'solid_fill = %s\ninvert_input = %s\ngpus = %s\ninvert_input.gpu = 3\n' "$reorderHome-fx/solid_fill.fx" "$reorderHome-fx/invert_input.fx" \
            "$other,$third" >>"$reorderHome/.config/vkdaedalus/vkdaedalus.conf"
        rm -f "$reorderMap"
        background drive_gpu_reorder "$reorder" "$reorderMap" "$reorderHome/.config/vkdaedalus/profiles/default.conf"
        driver=$!
        run "$reorder" "$reorderHome" "$every" "$BASALT:$VALIDATION" 1 1 -- env VKDAEDALUS_UI_MAP="$reorderMap" vkcube --c "$UI_FRAMES" --wsi xcb --width 1280 --height 720
        driver_finished "drive_gpu_reorder"
        while IFS=$'\t' read -r verdict caseName detail; do
            record "$verdict" "$caseName" "$detail"
        done <"$reorder.ui"
        if ! why=$(plain_verdict "$reorder" "$control"); then
            record FAIL "GPUs reordered and removed mid-game, cleanly" "$why"
        fi
    else
        record SKIP "the chain crosses GPU 2 then GPU 3" "this machine has no third GPU in the instance"
    fi
    DEVICE_SELECT=()
}

runs_without_linear_filter() {
    local icd="$1" dir="$2" control="$3" name="an unfilterable float format is sampled without filtering" log="$2/hide-linear-filter.log"
    local home="$2/hide-linear-filter-home" fx="$2/hide-linear-filter-home-fx" why
    fresh_home "$home" unfiltered_float
    cp "$VKB_FIXTURES/effects/unfiltered_float.fx" "$fx/"
    printf 'unfiltered_float = %s\n' "$fx/unfiltered_float.fx" >>"$home/.config/vkdaedalus/vkdaedalus.conf"
    run "$log" "$home" "$icd" "$BASALT:$FAULT:$VALIDATION" 1 1 -- \
        env FAULT_INJECT_HIDE_LINEAR_FILTER=100 vkcube --c "$CONFIG_FRAMES" --wsi xcb
    if ! why=$(plain_verdict "$log" "$control"); then
        record FAIL "$name" "$why"
    elif [ "$(count 'fault_inject: hid linear filtering of format 100' "$log")" -lt 1 ]; then
        record FAIL "$name" "the fault layer never hid linear filtering of R32_SFLOAT, so nothing was tested"
    elif [ "$(count 'sampler UnfilteredSampler: format 100 cannot be filtered on this GPU, so it is sampled without filtering' "$log")" -lt 1 ] \
        || [ "$(count 'texture UnfilteredTex: format 100 cannot be filtered on this GPU, so its mip levels are made without filtering' "$log")" -lt 1 ]; then
        record FAIL "$name" "the layer did not say the sampler and the mip levels run without filtering on this GPU"
    elif [ "$(count 'creating effect [0-9]+: unfiltered_float' "$log")" -ne 1 ]; then
        record FAIL "$name" "the effect was built $(count 'creating effect [0-9]+: unfiltered_float' "$log") times, want 1"
    else
        record PASS "$name" "with linear filtering of R32_SFLOAT hidden, the effect ran with its sampler and mip levels unfiltered and said so, validation clean"
    fi
}

fail_once() {
    local icd="$1" dir="$2" control="$3" call="$4" nth="$5" from="$6" report="$7" what="$8" chain="${9:-}" refuse="${10:-}"
    local name="fails once: $what ($call)" log="$dir/fault-once-$4-$5-$6.log" home="$dir/fault-once-$4-$5-$6-home"
    local counted_from="$from" arm="" presser="" armer="" armed=1 injected why
    fresh_home "$home" "$chain"
    printf 'overlayKey = %s\nreloadKey = %s\n' "$OVERLAY_KEY" "$RELOAD_KEY" >"$home/.config/vkdaedalus/settings.conf"
    if [ "$from" = overlay ]; then
        counted_from=acquire arm="$dir/fault-once-$call-$nth.arm"
        background press_overlay_key "$log"
        presser=$!
        { wait_for_line 'overlay recorded a frame' 5 "$log" && touch "$arm"; } &
        armer=$!
    elif [ "$from" = reload ]; then
        counted_from=acquire arm="$dir/fault-once-$call-$nth.arm"
        { wait_for_line 'present cycle' 20 "$log" && touch "$arm" && wait_for_line 'present cycle' 30 "$log" \
            && hold_key_until "$RELOAD_KEY" 'reload key pressed' 1 "$log"; } &
        armer=$!
    fi
    run "$log" "$home" "$icd" "$BASALT:$FAULT:$VALIDATION" 1 1 -- \
        env FAULT_INJECT_FUNCTION="$call" FAULT_INJECT_NTH="$nth" FAULT_INJECT_FROM="$counted_from" \
        FAULT_INJECT_ARM="$arm" FAULT_INJECT_REFUSE="$refuse" FAULT_INJECT_CALLER="$LAYER_LIBRARY" vkcube --c "$FRAMES" --wsi xcb
    if [ -n "$presser" ] && ! wait "$presser"; then armed=0; fi
    if [ -n "$armer" ] && ! wait "$armer"; then armed=0; fi
    injected=$(count "fault_inject: failed $call called from" "$log")
    if ! why=$(exited_cleanly "$log"); then
        record FAIL "$name" "$why"
    elif [ "$armed" -ne 1 ]; then
        record FAIL "$name" "the fault was never armed: the overlay never drew, or the reload key went unseen"
    elif ! why=$(validation_verdict "$log" "$control"); then
        record FAIL "$name" "$why"
    elif [ "$injected" -ne 1 ]; then
        record FAIL "$name" "$injected calls were failed, want exactly 1"
    elif [ "$(count 'trace: the layer submitted a frame' "$log")" -ne "$(count 'trace: present cycle' "$log")" ]; then
        record FAIL "$name" "the layer submitted $(count 'trace: the layer submitted a frame' "$log") frames for $(count 'trace: present cycle' "$log") presents, so the rest went out without the game's frame"
    elif [ "$(count "$report" "$log")" -lt 1 ]; then
        record FAIL "$name" "the layer never reported '$report', so call $nth counted from $from was not $what or went unreported"
    elif [ "$(count 'ImGui fence wait timed out' "$log")" -ne 0 ]; then
        record FAIL "$name" "the overlay timed out on its own fence $(count 'ImGui fence wait timed out' "$log") times, so a fence the failed call left unsignalled never came back"
    else
        record PASS "$name" "the layer reported it and the game ran its $FRAMES frames on, validation clean"
    fi
}

layer_frame_clock() {
    awk '/frame statistics: [0-9]+ present intervals over [0-9.]+ ms/ { for (i = 2; i < NF; i++) { if ($i == "present" && $(i + 1) == "intervals") n += $(i - 1); if ($i == "over") ms += $(i + 1) } } END { printf "%d %.3f\n", n, ms }' "$1"
}

frame_cost() {
    local icd="$1" dir="$2" mode="$3" enable="$4" n="$5" wsi="$6" home="${7:-$2/perf-home}" notice="${8:-never}" tag="${9:-}" frames start end why log short=0 long=0
    for frames in "$PERF_FRAMES" "$((PERF_FRAMES * 3))"; do
        log="$dir/perf-$wsi-$enable-$n-$frames$tag.log"
        start=$(date +%s%N)
        run "$log" "$home" "$icd" "" "$enable" 0 -- env VKDAEDALUS_LAUNCH_NOTICE="$notice" vkcube --c "$frames" --present_mode "$mode" --wsi "$wsi"
        end=$(date +%s%N)
        if ! why=$(exited_cleanly "$log"); then
            echo "$why"
            return 1
        fi
        if [ "$frames" = "$PERF_FRAMES" ]; then short=$((end - start)); else long=$((end - start)); fi
    done
    awk -v s="$short" -v l="$long" -v f="$PERF_FRAMES" 'BEGIN { printf "%.4f\n", (l - s) / (2 * f) / 1000000 }'
}

frame_clock_case() {
    local icd="$1" dir="$2" mode="$3" wsi="$4" name="the layer's frame clock, $4" frames=$((PERF_FRAMES * 3)) n log why above swapchains presents span clock gap gaps="" limit
    local LOG_LEVEL=info
    for n in 1 2 3; do
        log="$dir/frame-clock-$wsi-$n.log"
        run "$log" "$dir/perf-home" "$icd" "$FAULT:$BASALT" 1 1 -- env FAULT_INJECT_COUNT_PRESENTS=1 vkcube --c "$frames" --present_mode "$mode" --wsi "$wsi"
        if ! why=$(exited_cleanly "$log"); then
            record FAIL "$name" "$why"
            return
        fi
        above=$(awk -v f="\"$FAULT\"" -v b="\"$BASALT\"" '
            /Insert instance layer/ { if (index($0, f) && !countingAt) countingAt = NR; if (index($0, b) && !layerAt) layerAt = NR }
            END { print (countingAt && layerAt && layerAt < countingAt) ? "yes" : "no" }' "$log")
        swapchains=$(count 'frame statistics: [0-9]+ present intervals over' "$log")
        presents=$(sed -n 's/^fault_inject: counted \([0-9]*\) presents over [0-9.]* ms$/\1/p' "$log" | tail -1)
        span=$(sed -n 's/^fault_inject: counted [0-9]* presents over \([0-9.]*\) ms$/\1/p' "$log" | tail -1)
        clock=$(layer_frame_clock "$log")
        if [ "$above" != yes ]; then
            record FAIL "$name" "the counting layer is not above the layer in the loader chain, so the two clocks do not time the same calls"
            return
        elif [ -z "$presents" ] || [ "$swapchains" -ne 1 ]; then
            record FAIL "$name" "run $n: the counting layer reported ${presents:-nothing}, the layer logged frame statistics for $swapchains swapchains, want one"
            return
        elif [ "${clock%% *}" -ne $((presents - 1)) ]; then
            record FAIL "$name" "run $n: the layer counted ${clock%% *} present intervals for the $presents presents the layer above it counted, want $((presents - 1))"
            return
        fi
        gap=$(awk -v a="$span" -v b="${clock#* }" 'BEGIN { d = a - b; printf "%.3f", (d < 0 ? -d : d) }')
        gaps="$gaps$gap"$'\n'
    done
    gap=$(sort -n <<<"${gaps%$'\n'}" | sed -n 2p)
    limit=$(awk -v s="$span" 'BEGIN { printf "%.3f", 2 + s / 1000 }')
    if awk -v g="$gap" -v l="$limit" 'BEGIN { exit !(g > l) }'; then
        record FAIL "$name" "the layer's summed present intervals and the span the layer above it timed for the same $frames presents differ by $gap ms (median of 3), more than $limit ms"
    else
        record PASS "$name" "the layer counted one interval fewer than the $frames presents the layer above it timed, and their sums agree within $gap ms (median of 3, limit $limit ms)"
    fi
}

spread() {
    sort -n | awk '{ v[NR] = $1 } END { printf "%.4f (%.4f..%.4f)", v[int((NR + 1) / 2)], v[1], v[NR] }'
}

default_costs() {
    local icd="$1" dir="$2" mode="$3" kind n value size bytes why slots=""
    local -A figures=() effect=([full]=half_alu [half]=half_alu [still]=uniform_smoke [live]=uniform_smoke)
    local -A says=([full]='half_alu.fx, full precision$' [half]='half_alu.fx, half precision$' [still]='uniform_smoke.fx, full precision$' [live]='uniform_smoke.fx, full precision, live uniforms$')
    local LOG_LEVEL=debug
    for kind in full half still live; do
        fresh_home "$dir/perf-home-$kind" "${effect[$kind]}"
        cp "$VKB_FIXTURES/effects/half_alu.fx" "$dir/perf-home-$kind-fx/"
        printf 'half_alu = %s\n' "$dir/perf-home-$kind-fx/half_alu.fx" >>"$dir/perf-home-$kind/.config/vkdaedalus/vkdaedalus.conf"
    done
    printf 'half_alu.halfPrecision = true\n' >>"$dir/perf-home-half/.config/vkdaedalus/vkdaedalus.conf"
    printf 'liveReshadeUniforms = true\n' >>"$dir/perf-home-live/.config/vkdaedalus/settings.conf"
    for kind in full half still; do printf 'liveReshadeUniforms = false\n' >>"$dir/perf-home-$kind/.config/vkdaedalus/settings.conf"; done
    for kind in full half still live; do
        run "$dir/perf-$kind-warmup.log" "$dir/perf-home-$kind" "$icd" "" 1 0 -- vkcube --c "$PERF_FRAMES" --present_mode "$mode" --wsi "$native_wsi"
        if ! why=$(exited_cleanly "$dir/perf-$kind-warmup.log"); then
            record FAIL "per-frame cost of the defaults" "the $kind warm-up: $why"
            return
        elif [ "$(count "compiling .*${says[$kind]}" "$dir/perf-$kind-warmup.log")" -lt 1 ]; then
            record FAIL "per-frame cost of the defaults" "the $kind warm-up never compiled ${effect[$kind]} as $(sed 's/^[^,]*, //; s/\$$//' <<<"${says[$kind]}"), so its figure would not be that setting's"
            return
        fi
    done
    for size in 1920x1080 3840x2160; do
        run "$dir/perf-slot-$size.log" "$dir/perf-home-full" "$icd" "" 1 0 -- vkcube --c 60 --width "${size%x*}" --height "${size#*x}" --wsi "$native_wsi"
        bytes=$(sed -n 's/.*fake image size: \([0-9]*\)$/\1/p' "$dir/perf-slot-$size.log" | head -1)
        if [ -n "$bytes" ]; then slots="$slots${slots:+, }$size $((bytes / 1048576)) MB"; else slots="$slots${slots:+, }$size not reported"; fi
    done
    record MEAS "memory of one effect image" "$slots; the layer keeps one per swapchain image for each effect, up to the effect limit"
    LOG_LEVEL=warn
    for n in 1 2 3 4 5; do
        for kind in full half still live; do
            if ! value=$(frame_cost "$icd" "$dir" "$mode" 1 "$n" "$native_wsi" "$dir/perf-home-$kind" never "-$kind"); then
                record FAIL "per-frame cost of the defaults" "a $kind run: $value"
                return
            fi
            figures[$kind]+="$value"$'\n'
        done
    done
    record MEAS "per-frame cost of half precision, $native_wsi" "$(printf '%s' "${figures[half]}" | spread) ms/frame with half precision, $(printf '%s' "${figures[full]}" | spread) with full, an effect doing 256 min16float multiply-adds a pixel, median of 5 at 1800 minus 600 frames (present mode $mode)"
    record MEAS "per-frame cost of instant ReShade sliders, $native_wsi" "$(printf '%s' "${figures[live]}" | spread) ms/frame with instant sliders, $(printf '%s' "${figures[still]}" | spread) without, uniform_smoke, median of 5 at 1800 minus 600 frames (present mode $mode)"
}

measure() {
    local icd="$1" dir="$2" mode without=() with=() value why run_name wsi bench
    local LOG_LEVEL=warn
    if ! mode=$(present_mode_of "$icd" "$dir"); then
        record FAIL "per-frame cost" "vulkaninfo failed: $(tail -1 "$dir/vulkaninfo.err")"
        return
    fi
    fresh_home "$dir/perf-home" macro_spacing
    run "$dir/perf-warmup.log" "$dir/perf-home" "$icd" "" 1 0 -- vkcube --c "$PERF_FRAMES" --present_mode "$mode" --wsi "$native_wsi"
    if ! why=$(exited_cleanly "$dir/perf-warmup.log"); then
        record FAIL "per-frame cost" "the warm-up run: $why"
        return
    fi
    for wsi in wayland xcb; do
        without=()
        with=()
        for run_name in 1 2 3 4 5; do
            if ! value=$(frame_cost "$icd" "$dir" "$mode" 0 "$run_name" "$wsi"); then
                record FAIL "per-frame cost, $wsi" "a run without the layer: $value"
                return
            fi
            without+=("$value")
            if ! value=$(frame_cost "$icd" "$dir" "$mode" 1 "$run_name" "$wsi"); then
                record FAIL "per-frame cost, $wsi" "a run with the layer: $value"
                return
            fi
            with+=("$value")
        done
        record MEAS "per-frame cost, one effect, $wsi" "$(printf '%s\n' "${without[@]}" | spread) ms/frame without, $(printf '%s\n' "${with[@]}" | spread) with, median of 5 at 1800 minus 600 frames (present mode $mode)"
        frame_clock_case "$icd" "$dir" "$mode" "$wsi"
    done
    local hud=() notice=() noticeLeft=0
    rm -rf "$dir/perf-home-hud"
    cp -r "$dir/perf-home" "$dir/perf-home-hud"
    printf 'hud = true\nhudItems = fps,frametime,slowest\n' >>"$dir/perf-home-hud/.config/vkdaedalus/settings.conf"
    LOG_LEVEL=info
    run "$dir/perf-hud-proof.log" "$dir/perf-home-hud" "$icd" "" 1 0 -- env VKDAEDALUS_LAUNCH_NOTICE=never vkcube --c "$((PERF_FRAMES * 3))" --present_mode "$mode" --wsi "$native_wsi"
    run "$dir/perf-notice-proof.log" "$dir/perf-home" "$icd" "" 1 0 -- env VKDAEDALUS_LAUNCH_NOTICE=every vkcube --c "$((PERF_FRAMES * 3))" --present_mode "$mode" --wsi "$native_wsi"
    LOG_LEVEL=warn
    noticeLeft=$(count 'launch notice left' "$dir/perf-notice-proof.log")
    with=()
    for run_name in 1 2 3 4 5; do
        if ! value=$(frame_cost "$icd" "$dir" "$mode" 1 "$run_name" "$native_wsi" "$dir/perf-home" never -plain); then
            record FAIL "per-frame cost of the HUD and the notice" "a run with the layer: $value"
            return
        fi
        with+=("$value")
        if ! value=$(frame_cost "$icd" "$dir" "$mode" 1 "$run_name" "$native_wsi" "$dir/perf-home-hud" never -hud); then
            record FAIL "per-frame cost of the HUD and the notice" "a run with the HUD: $value"
            return
        fi
        hud+=("$value")
        if ! value=$(frame_cost "$icd" "$dir" "$mode" 1 "$run_name" "$native_wsi" "$dir/perf-home" every -notice); then
            record FAIL "per-frame cost of the HUD and the notice" "a run with the launch notice: $value"
            return
        fi
        notice+=("$value")
    done
    local drawn
    drawn=$(awk 'sub(/.*HUD shown while the overlay is closed: /, "") { print; exit }' "$dir/perf-hud-proof.log")
    if [ -z "$drawn" ]; then
        record FAIL "per-frame cost of the HUD" "an info-level run with hud = true never drew the HUD, so the figure would not be the HUD's"
    elif [ "${drawn%% *}" != 3 ]; then
        record FAIL "per-frame cost of the HUD" "the HUD drew $drawn, not the three items asked, so the figure would not be what it names"
    else
        record MEAS "per-frame cost of the HUD, $native_wsi" "$(printf '%s\n' "${hud[@]}" | spread) ms/frame with the HUD drawing $drawn, $(printf '%s\n' "${with[@]}" | spread) without it, median of 5 at 1800 minus 600 frames (present mode $mode)"
    fi
    if [ "$(count 'launch notice shown' "$dir/perf-notice-proof.log")" -lt 1 ]; then
        record FAIL "per-frame cost of the launch notice" "an info-level run with VKDAEDALUS_LAUNCH_NOTICE=every never showed the notice"
    else
        record MEAS "per-frame cost of the launch notice, $native_wsi" "$(printf '%s\n' "${notice[@]}" | spread) ms/frame while it shows, $(printf '%s\n' "${with[@]}" | spread) without it, median of 5 at 1800 minus 600 frames (present mode $mode)$([ "$noticeLeft" -gt 0 ] && echo ", though it left before an info-level run of the same length ended")"
    fi
    default_costs "$icd" "$dir" "$mode"
    if bench=$(env VK_DRIVER_FILES="$icd" "${DEVICE_SELECT[@]}" timeout -s KILL "$HANG_SECONDS" queue-wait-bench 2>&1); then
        record MEAS "queue drain vs own fence" "$(tail -1 <<<"$bench")"
    else
        record FAIL "queue drain vs own fence" "$(tail -1 <<<"$bench")"
    fi
}

device_records() {
    awk -F' = ' '
        function record() { return f["deviceType"] "|" f["vendorID"] "|" f["deviceID"] "|" f["driverID"] "|" f["deviceName"] "|" f["driverInfo"] }
        /^GPU[0-9]+:$/ { if (seen) print record(); seen = 1; delete f; next }
        seen && NF == 2 { key = $1; gsub(/^[ \t]+|[ \t]+$/, "", key); f[key] = $2 }
        END { if (seen) print record() }
    ' <<<"$1"
}

finish_device() {
    printf '%s\n' "${results[@]}"
    echo "${#results[@]}" >"$1/count-$2"
    if [ "$gpu_failed" -eq 0 ]; then echo PASS >"$1/verdict-$2"; else echo FAIL >"$1/verdict-$2"; fi
}

test_device() {
    local icd="$1" device="$2" driver="$3" dir="$4" phase="$5" pinned session_device tag=""
    local PRIVATE_RUNTIME="$dir/runtime-$phase" PRIVATE_DISPLAY=""
    mkdir -p "$dir"
    mkdir -m 700 "$PRIVATE_RUNTIME"
    results=()
    gpu_failed=0
    if [ "$phase" != all ]; then tag=" [$phase]"; fi
    echo
    echo "=== $device, $driver ($icd${DEVICE_SELECT[*]:+, ${DEVICE_SELECT[*]}})$tag ==="
    pinned=$(device_records "$(env -u DISPLAY -u WAYLAND_DISPLAY XDG_RUNTIME_DIR="$PRIVATE_RUNTIME" VK_DRIVER_FILES="$icd" "${DEVICE_SELECT[@]}" vulkaninfo --summary 2>&1)")
    LANE_GPU_ID=$(awk -F'|' 'NF { print $2 ":" $3; exit }' <<<"$pinned")
    if ! awk -F'|' -v d="$device" 'NF { n++; if ($5 != d) other = 1 } END { exit (n >= 1 && !other) ? 0 : 1 }' <<<"$pinned"; then
        record FAIL "Vulkan sees only this GPU" "with ${DEVICE_SELECT[*]:-no pin} the driver lists: $(cut -d'|' -f5 <<<"$pinned" | paste -sd ',' -)"
        finish_device "$dir" "$phase"
        return
    fi
    fresh_home "$dir/layer-loads-home"
    run "$dir/layer-loads.log" "$dir/layer-loads-home" "$icd" "" 1 0 -- vulkaninfo --summary
    if ! awk -v name="\"$BASALT\"" -v lib="$VKB_LAYER/lib/" '/Insert instance layer/ && index($0, name) { if (index($0, lib)) ok = 1 } END { exit ok ? 0 : 1 }' "$dir/layer-loads.log"; then
        record FAIL "the layer under test is the one that loads" "$(grep -m1 -F "Insert instance layer \"$BASALT\"" "$dir/layer-loads.log" || echo "the loader never inserted $BASALT"), want it from $VKB_LAYER"
        finish_device "$dir" "$phase"
        return
    fi
    record PASS "the layer under test is the one that loads" "the loader inserted $BASALT from $VKB_LAYER"
    if ! start_session "$dir" "$icd"; then
        record FAIL "the private session starts on this GPU" "$session_problem"
        stop_session
        finish_device "$dir" "$phase"
        return
    fi
    session_device=$(sed -n -E "s/.*Vulkan device: '(.*)'/\1/p" "$dir/weston.log" | sed -n 1p)
    if [ "$session_device" != "$device" ]; then
        record FAIL "the session runs on this GPU" "weston renders on ${session_device:-no GPU}, so every lane would present across GPUs"
        stop_session
        finish_device "$dir" "$phase"
        return
    fi
    if [ "$(count 'Failed to initialize glamor' "$dir/xwayland.log")" -ne 0 ]; then
        record FAIL "the session's X11 has GPU access" "Xwayland fell back to software ($(sed -n 1p "$dir/xwayland.log")), so no Vulkan X11 swapchain can present"
        stop_session
        finish_device "$dir" "$phase"
        return
    fi
    if [ "$phase" != measure ]; then
        lane_e2e "$icd" "$dir"
        lane_framegen "$icd" "$dir"
        lane_surfaces "$icd" "$dir"
        lane_overlay "$icd" "$dir"
        lane_ui "$icd" "$dir"
        lane_focus "$icd" "$dir"
        lane_controls "$icd" "$dir"
        lane_config "$icd" "$dir"
        lane_pixels "$icd" "$dir"
        lane_settings_reach_shaders "$icd" "$dir"
        lane_faults "$icd" "$dir"
        lane_offload "$icd" "$dir"
        lane_layers "$icd" "$dir"
    fi
    if [ "$phase" != lanes ]; then
        measure "$icd" "$dir"
    fi
    stop_session
    finish_device "$dir" "$phase"
}

select_device() {
    DEVICE_SELECT=()
    if [ -n "$1" ]; then DEVICE_SELECT=(VK_LOADER_DEVICE_ID_FILTER="$1"); fi
}

DEVICE_SELECT=()
gpus=()
for icd in "${icds[@]}"; do
    if summary=$(env -u DISPLAY -u WAYLAND_DISPLAY XDG_RUNTIME_DIR="$PRIVATE_RUNTIME" VK_DRIVER_FILES="$icd" vulkaninfo --summary 2>&1); then probe=ok; else probe="vulkaninfo exit $?"; fi
    records=$(device_records "$summary")
    if [ -z "$records" ]; then
        echo "skip  $(basename "$icd"): no device ($probe: $(grep -m1 -E 'ERROR|error' <<<"$summary" || echo 'no error text'))"
        continue
    fi
    while IFS='|' read -r -u 3 type vendor_id device_id _ device driver; do
        case "$type" in
            PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU | PHYSICAL_DEVICE_TYPE_DISCRETE_GPU | PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU)
                gpus+=("$icd|$vendor_id|$device_id|$device|$driver")
                ;;
            PHYSICAL_DEVICE_TYPE_CPU)
                echo "skip  $(basename "$icd"): software renderer ($device), not a GPU"
                ;;
            *)
                echo "skip  $(basename "$icd"): unrecognised device type $type ($device)"
                ;;
        esac
    done 3<<<"$records"
done

physical_gpus=$(printf '%s\n' "${gpus[@]}" | awk -F'|' 'NF { print $2 ":" $3 }' | sort -u | awk 'NF { n++ } END { print n + 0 }')
lost_gpus=""
if [ "$explicit_icds" -eq 0 ]; then
    kernel_gpus=()
    for node in /sys/class/drm/renderD*; do
        [ -e "$node/device/driver" ] || continue
        node_driver=$(basename "$(readlink -f "$node/device/driver")")
        case "$node_driver" in vgem | vkms) continue ;; esac
        kernel_gpus+=("$(basename "$node") ($node_driver)")
    done
    vulkan_gpus=$(printf '%s\n' "${gpus[@]}" | awk -F'|' 'NF { seen[$1 SUBSEP $2 ":" $3]++ } END { for (k in seen) { split(k, p, SUBSEP); if (seen[k] > most[p[2]]) most[p[2]] = seen[k] } for (id in most) n += most[id]; print n + 0 }')
    if [ "${#kernel_gpus[@]}" -gt "$vulkan_gpus" ]; then
        lost_gpus="the kernel shows ${#kernel_gpus[@]} GPU(s), ${kernel_gpus[*]}, and Vulkan opened $vulkan_gpus, so a driver did not start"
        echo
        printf '  %-5s %-36s %s\n' FAIL "every GPU the kernel shows opens" "$lost_gpus"
    fi
fi
declare -A tested=()
runs=()
for gpu in "${gpus[@]}"; do
    IFS='|' read -r icd vendor_id device_id device driver <<<"$gpu"
    if [ -n "${tested[$icd|$vendor_id:$device_id]:-}" ]; then
        echo "skip  $(basename "$icd"): another $device, identical by vendor and device ID to one already tested, so it cannot be pinned on its own"
        continue
    fi
    tested[$icd|$vendor_id:$device_id]=1
    filter=""
    if [ "$physical_gpus" -gt 1 ]; then filter="$device_id"; fi
    runs+=("$icd|$device|$driver|$WORK/gpu-$((${#runs[@]} + 1))|$filter")
done

work_stray=""
if [ "$parallel" -eq 1 ]; then
    phases=(lanes measure)
    lane_pids=()
    for run_entry in "${runs[@]}"; do
        IFS='|' read -r icd device driver dir filter <<<"$run_entry"
        select_device "$filter"
        mkdir -p "$dir"
        (
            compositor_pids=()
            trap 'report_early_stop "the lanes on this GPU" "$?" "$BASH_COMMAND"; stop_session' EXIT
            test_device "$icd" "$device" "$driver" "$dir" lanes
        ) >"$dir/report-lanes.txt" 2>&1 &
        lane_pids+=("$!")
    done
    for i in "${!lane_pids[@]}"; do
        rc=0
        wait "${lane_pids[i]}" || rc=$?
        IFS='|' read -r icd device driver dir filter <<<"${runs[i]}"
        cat "$dir/report-lanes.txt"
        if [ "$rc" -ne 0 ]; then
            printf '  %-5s %-36s %s\n' FAIL "the GPU's lanes finished" "its parallel run ended with status $rc"
            echo FAIL >"$dir/verdict-lanes"
        fi
    done
    lane_pids=()
    work_stray=$(find "$WORK" -mindepth 1 -maxdepth 1 ! -name runtime ! -name weston.ini ! -name keep ! -name 'gpu-*' -printf '%f ')
    if [ -n "$work_stray" ]; then
        echo
        printf '  %-5s %-36s %s\n' FAIL "the parallel runs kept to their folders" "found at the top of the work folder: ${work_stray% }"
    fi
    for run_entry in "${runs[@]}"; do
        IFS='|' read -r icd device driver dir filter <<<"$run_entry"
        select_device "$filter"
        test_device "$icd" "$device" "$driver" "$dir" measure
    done
else
    phases=(all)
    for run_entry in "${runs[@]}"; do
        IFS='|' read -r icd device driver dir filter <<<"$run_entry"
        select_device "$filter"
        test_device "$icd" "$device" "$driver" "$dir" all
    done
fi

if [ -n "$gallery" ]; then
    missing=()
    for state in "${GALLERY_STATES[@]}"; do
        if [ ! -s "$GALLERY_PARTIAL/$state.png" ]; then missing+=("$state"); fi
    done
    rm -rf "$gallery"
    mv "$GALLERY_PARTIAL" "$gallery"
    echo
    echo "gallery: $((${#GALLERY_STATES[@]} - ${#missing[@]})) of ${#GALLERY_STATES[@]} overlay states kept in $gallery"
    if [ "${#missing[@]}" -gt 0 ]; then echo "gallery: not reached on the first GPU: ${missing[*]}"; fi
fi

real_gpus=${#runs[@]}
passed_gpus=0
for run_entry in "${runs[@]}"; do
    IFS='|' read -r icd device driver dir filter <<<"$run_entry"
    failed=0
    counted=0
    for phase in "${phases[@]}"; do
        if [ ! -f "$dir/verdict-$phase" ] || [ "$(cat "$dir/verdict-$phase")" != PASS ]; then failed=1; fi
        if [ -f "$dir/count-$phase" ]; then counted=$((counted + $(cat "$dir/count-$phase"))); fi
    done
    if [ "$counted" -eq 0 ]; then
        echo
        echo "=== $device ==="
        printf '  %-5s %-36s %s\n' FAIL "the lanes ran" "no lane recorded a result on this GPU, so nothing on it was tested"
        failed=1
    fi
    if [ "$failed" -eq 0 ]; then passed_gpus=$((passed_gpus + 1)); fi
done

echo
if [ "$real_gpus" -eq 0 ]; then
    echo "LIVE FAIL: no real GPU found, so nothing was tested on hardware"
    exit 1
fi
if [ "$passed_gpus" -ne "$real_gpus" ] || [ -n "$work_stray" ] || [ -n "$lost_gpus" ]; then
    keep_work=1
    echo "LIVE FAIL: $passed_gpus of $real_gpus GPU(s) passed every lane${work_stray:+, and the parallel runs wrote outside their folders: ${work_stray% }}${lost_gpus:+, and $lost_gpus}"
    exit 1
fi
echo "LIVE PASS: $passed_gpus of $real_gpus GPU(s) passed every lane"
