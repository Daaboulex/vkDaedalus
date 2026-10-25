project('vkdaedalus', ['c', 'cpp'], default_options: ['c_std=c11', 'cpp_std=c++20'])
if host_machine.cpu_family() == 'x86'
    add_global_arguments('-msse2', '-mfpmath=sse', language: ['c', 'cpp'])
endif
add_project_arguments('-Werror=return-type', language: ['c', 'cpp'])

layer_api_major = 1
layer_api_minor = 3
layer_api_patch = 223
layer_api_version = '@0@.@1@.@2@'.format(layer_api_major, layer_api_minor, layer_api_patch)

cpp = meson.get_compiler('cpp')

library_arch = cpp.sizeof('void*') == 8 ? '64' : '32'

vkBasalt_include_path = ''

stb_dep = subproject('stb').get_variable('stb_dep')
imgui_dep = subproject('imgui').get_variable('imgui_dep')
imgui_filebrowser_dep = subproject('imgui-filebrowser').get_variable('imgui_filebrowser_dep')
reshade_dep = subproject('reshade').get_variable('reshade_dep')
anticheat_rules_ini = subproject('filedetectionrulesets').get_variable('anticheat_rules_ini')
tomlplusplus_dep = subproject('tomlplusplus').get_variable('tomlplusplus_dep')
reshade_fxh = subproject('reshade-shaders').get_variable('reshade_fxh')

lib_dir = get_option('libdir')
ld_lib_dir_vkdaedalus = ''

if get_option('append_libdir_vkdaedalus')
    lib_dir = join_paths(lib_dir, 'vkdaedalus')
    ld_lib_dir_vkdaedalus = get_option('prefix') + '/$LIB/vkdaedalus/'
endif

if get_option('with_so')
    subdir('src')

    test('shaders-compile-and-cache-round-trip', test_shaders_exe,
        args : [
            '--cache-verify',
            meson.project_source_root() / 'test' / 'language',
            meson.project_source_root() / 'test' / 'lut',
            meson.project_source_root() / 'test' / 'effects',
            meson.project_source_root() / 'test' / 'live_uniforms',
            meson.project_source_root() / 'test' / 'textures',
        ],
        env : {'XDG_CACHE_HOME' : meson.current_build_dir() / 'test-xdg-cache'},
        timeout : 300)

    test('live-uniform-lowering', test_shaders_exe,
        args : [
            '--require-live-uniforms',
            '--cache-verify',
            meson.project_source_root() / 'test' / 'live_uniforms',
        ],
        env : {'XDG_CACHE_HOME' : meson.current_build_dir() / 'test-live-uniform-cache'},
        timeout : 60)

    input_map_test = executable('reshade-input-map-test',
        'test/reshade_input_map_test.cpp',
        include_directories : include_directories('src'),
        dependencies : [reshade_dep])
    test('reshade-input-map', input_map_test)

    reshade_pass_utils_test = executable('reshade-pass-utils-test',
        'test/reshade_pass_utils_test.cpp',
        include_directories : include_directories('src'),
        dependencies : [reshade_dep])
    test('reshade-pass-utils', reshade_pass_utils_test)

    device_limits_test = executable('device-limits-test',
        'test/device_limits_test.cpp',
        include_directories : include_directories('src'))
    test('device-limits', device_limits_test)

    format_features_test = executable('format-features-test',
        'test/format_features_test.cpp',
        include_directories : include_directories('src'))
    test('format-features', format_features_test)

    gpu_assignment_test = executable('gpu-assignment-test',
        'test/gpu_assignment_test.cpp',
        include_directories : include_directories('src'))
    test('gpu-assignment', gpu_assignment_test)

    offload_hand_off_test = executable('offload-hand-off-test',
        'test/offload_hand_off_test.cpp', 'src/hand_off.cpp', 'src/logger.cpp',
        include_directories : include_directories('src'))
    test('offload-hand-off', offload_hand_off_test)

    frame_generation_test = executable('frame-generation-test',
        'test/frame_generation_test.cpp', 'src/frame_generation.cpp',
        include_directories : include_directories('src'),
        dependencies : [tomlplusplus_dep, dl_dep])
    test('frame-generation', frame_generation_test,
        args : [meson.current_build_dir() / 'test-frame-generation'])

    reshade_uniform_write_test = executable('reshade-uniform-write-test',
        'test/reshade_uniform_write_test.cpp', 'src/logger.cpp',
        include_directories : include_directories('src'),
        dependencies : [reshade_dep])
    test('reshade-uniform-write', reshade_uniform_write_test)

    shader_cache_bounds_test = executable('shader-cache-bounds-test',
        'test/shader_cache_bounds_test.cpp', 'src/shader_cache.cpp', 'src/logger.cpp',
        include_directories : include_directories('src'),
        dependencies : [reshade_dep, dl_dep])
    test('shader-cache-bounds', shader_cache_bounds_test)

    shader_cache_disk_test = executable('shader-cache-disk-test',
        'test/shader_cache_disk_test.cpp', 'src/shader_cache.cpp', 'src/logger.cpp',
        include_directories : include_directories('src'),
        dependencies : [reshade_dep, dl_dep])
    test('shader-cache-disk', shader_cache_disk_test,
        args : [meson.project_source_root() / 'test' / 'effects' / 'solid_fill.fx'],
        env : {'XDG_CACHE_HOME' : meson.current_build_dir() / 'test-disk-cache'})

    layer_compile_env_test = executable('layer-compile-env-test',
        'test/layer_compile_env_test.cpp', 'src/shader_cache.cpp', 'src/logger.cpp',
        include_directories : include_directories('src'),
        dependencies : [reshade_dep, dl_dep])
    test('layer-compile-environment', layer_compile_env_test,
        args : [
            meson.project_source_root() / 'test' / 'language' / 'reshade_version.fx',
            meson.project_source_root() / 'test' / 'language' / 'gather_compat.fx',
        ],
        env : {'XDG_CACHE_HOME' : meson.current_build_dir() / 'test-layer-env-cache'})

    preprocessor_defs_test = executable('preprocessor-defs-test',
        'test/preprocessor_defs_test.cpp', 'src/reshade_parser.cpp', 'src/shader_cache.cpp', 'src/config.cpp',
        'src/config_serializer.cpp', 'src/logger.cpp',
        include_directories : include_directories('src'),
        dependencies : [reshade_dep, dl_dep])
    test('preprocessor-defs', preprocessor_defs_test,
        args : [meson.project_source_root() / 'test' / 'language' / 'layer_set_define.fx'],
        env : {
            'XDG_CACHE_HOME' : meson.current_build_dir() / 'test-preprocessor-defs-cache',
            'XDG_CONFIG_HOME' : meson.current_build_dir() / 'test-preprocessor-defs-config',
        })

    spirv_binding_test = executable('spirv-binding-test',
        'test/spirv_binding_test.cpp', 'src/shader_cache.cpp', 'src/logger.cpp',
        include_directories : include_directories('src'),
        dependencies : [reshade_dep, dl_dep])
    test('spirv-binding', spirv_binding_test,
        args : [meson.project_source_root() / 'test' / 'language' / 'binding_order.fx', '15'],
        env : {'XDG_CACHE_HOME' : meson.current_build_dir() / 'test-spirv-binding-cache'})

    compile_error_test = executable('compile-error-test',
        'test/compile_error_test.cpp', 'src/shader_cache.cpp', 'src/logger.cpp',
        include_directories : include_directories('src'),
        dependencies : [reshade_dep, dl_dep])
    test('compile-error', compile_error_test,
        args : [meson.project_source_root() / 'test' / 'broken' / 'store_typo.fx'],
        env : {'XDG_CACHE_HOME' : meson.current_build_dir() / 'test-compile-error-cache'})

    uses_depth_test = executable('uses-depth-test',
        'test/uses_depth_test.cpp', 'src/shader_cache.cpp', 'src/logger.cpp',
        include_directories : include_directories('src'),
        dependencies : [reshade_dep, dl_dep])
    test('uses-depth', uses_depth_test,
        args : [
            meson.project_source_root() / 'test' / 'effects' / 'depth_semantic_smoke.fx', 'depth',
            meson.project_source_root() / 'test' / 'language' / 'second_depth.fx', 'depth',
            meson.project_source_root() / 'test' / 'language' / 'depth_through_parameter.fx', 'depth',
            meson.project_source_root() / 'test' / 'effects' / 'uniform_smoke.fx', 'none',
            meson.project_source_root() / 'test' / 'language' / 'uncalled_depth_helper.fx', 'none',
        ],
        env : {'XDG_CACHE_HOME' : meson.current_build_dir() / 'test-uses-depth-cache'})

    lut_cube_test = executable('lut-cube-test',
        'test/lut_cube_test.cpp', 'src/lut_cube.cpp',
        include_directories : include_directories('src'))
    test('lut-cube', lut_cube_test, args : [meson.current_build_dir() / 'test-lut-cube'])

    dds_bounds_test = executable('dds-bounds-test',
        'test/dds_bounds_test.cpp', 'src/stb_image.c',
        include_directories : include_directories('src'),
        dependencies : [stb_dep])
    test('dds-bounds', dds_bounds_test,
        args : [meson.project_source_root() / 'test' / 'textures' / 'solid.dds'])

    swapchain_create_info_test = executable('swapchain-create-info-test',
        'test/swapchain_create_info_test.cpp', 'src/logger.cpp',
        include_directories : include_directories('src'))
    test('swapchain-create-info', swapchain_create_info_test)

    layer_queue_test = executable('layer-queue-test',
        'test/layer_queue_test.cpp',
        include_directories : include_directories('src'))
    test('layer-queue', layer_queue_test)

    config_own_write_test = executable('config-own-write-test',
        'test/config_own_write_test.cpp', 'src/config.cpp', 'src/config_serializer.cpp', 'src/logger.cpp',
        include_directories : include_directories('src'))
    test('config-own-write', config_own_write_test,
        args : [meson.current_build_dir() / 'test-config-own-write'],
        timeout : 60)

    config_values_test = executable('config-values-test',
        'test/config_values_test.cpp', 'src/config.cpp', 'src/config_serializer.cpp', 'src/logger.cpp',
        include_directories : include_directories('src'))
    test('config-values', config_values_test,
        args : [meson.current_build_dir() / 'test-config-values'])

    input_backend_test = executable('input-backend-test',
        'test/input_backend_test.cpp', 'src/wayland_display.cpp', 'src/logger.cpp',
        include_directories : include_directories('src'))
    test('input-backend', input_backend_test)

    ui_map_test = executable('ui-map-test',
        'test/ui_map_test.cpp', 'src/overlay/ui_map.cpp', 'src/logger.cpp',
        include_directories : include_directories('src'),
        dependencies : [imgui_dep])
    test('ui-map', ui_map_test, args : [meson.current_build_dir() / 'ui-map-test.map'])

    overlay_style_test = executable('overlay-style-test',
        'test/overlay_style_test.cpp', 'src/overlay/overlay_style.cpp', 'src/overlay/widgets.cpp', 'src/overlay/frame_draw.cpp', 'src/overlay/ui_map.cpp', 'src/logger.cpp', overlay_fonts,
        include_directories : include_directories('src'),
        dependencies : [imgui_dep])
    test('overlay-style', overlay_style_test)

    gpu_probe_names_test = executable('gpu-probe-names-test',
        'test/gpu_probe_names_test.cpp',
        include_directories : include_directories('src'))
    test('gpu-probe-names', gpu_probe_names_test)

    effect_compile_jobs_test = executable('effect-compile-jobs-test',
        'test/effect_compile_jobs_test.cpp', 'src/logger.cpp',
        include_directories : include_directories('src'),
        dependencies : [dependency('threads')])
    test('effect-compile-jobs', effect_compile_jobs_test, timeout : 60)

    background_work_test = executable('background-work-test',
        'test/background_work_test.cpp', 'src/logger.cpp',
        include_directories : include_directories('src'),
        dependencies : [dependency('threads')])
    test('background-work', background_work_test, timeout : 30)

    settings_depth_test = executable('settings-depth-test',
        'test/settings_depth_test.cpp', 'src/settings_manager.cpp', 'src/config_serializer.cpp', 'src/logger.cpp',
        include_directories : include_directories('src'))
    test('settings-depth', settings_depth_test,
        args : [meson.current_build_dir() / 'test-settings-depth-config'])

    compile_defines_test = executable('compile-defines-test',
        'test/compile_defines_test.cpp',
        include_directories : include_directories('src'),
        dependencies : [reshade_dep])
    test('compile-defines', compile_defines_test)

    reshade_effect_files_test = executable('reshade-effect-files-test',
        'test/reshade_effect_files_test.cpp',
        include_directories : include_directories('src'))
    test('reshade-effect-files', reshade_effect_files_test)

    reshade_texture_utils_test = executable('reshade-texture-utils-test',
        'test/reshade_texture_utils_test.cpp', 'src/stb_image_resize2.c',
        include_directories : include_directories('src'),
        dependencies : [stb_dep, reshade_dep])
    test('reshade-texture-utils', reshade_texture_utils_test)

    effect_timing_test = executable('effect-timing-test',
        'test/effect_timing_test.cpp',
        include_directories : include_directories('src'))
    test('effect-timing', effect_timing_test)

    latency_stats_test = executable('latency-stats-test',
        'test/latency_stats_test.cpp',
        include_directories : include_directories('src'))
    test('latency-stats', latency_stats_test, args : [files('LATENCY.md')])

    reshade_uniform_storage_test = executable('reshade-uniform-storage-test',
        'test/reshade_uniform_storage_test.cpp',
        include_directories : include_directories('src'),
        dependencies : [reshade_dep])
    test('reshade-uniform-storage', reshade_uniform_storage_test)

    live_uniform_policy_test = executable('live-uniform-policy-test',
        'test/live_uniform_policy_test.cpp',
        include_directories : include_directories('src'),
        dependencies : [reshade_dep])
    test('live-uniform-policy', live_uniform_policy_test)

    effect_selection_test = executable('effect-selection-test',
        'test/effect_selection_test.cpp',
        include_directories : include_directories('src'))
    test('effect-selection', effect_selection_test)

    anticheat_files_test = executable('anticheat-files-test',
        'test/anticheat_files_test.cpp', 'src/anticheat_files.cpp', 'src/config_serializer.cpp', 'src/logger.cpp',
        include_directories : include_directories('src'))
    test('anticheat-files', anticheat_files_test, args : [meson.current_build_dir() / 'test-anticheat-files'])

    conflicting_layer_stand_in = shared_library('vkbasalt', 'test/conflicting-layer/stand_in.c', install : false)
    conflicting_layer_test = executable('conflicting-layer-test',
        'test/conflicting_layer_test.cpp', 'src/util.cpp',
        include_directories : include_directories('src'),
        dependencies : [dl_dep])
    test('conflicting-layer', conflicting_layer_test, args : [conflicting_layer_stand_in.full_path()], depends : conflicting_layer_stand_in)

    logger_cost_test = executable('logger-cost-test',
        'test/logger_cost_test.cpp', 'src/logger.cpp',
        include_directories : include_directories('src'))
    test('logger-cost', logger_cost_test)

    state_log_test = executable('state-log-test',
        'test/state_log_test.cpp', 'src/logger.cpp',
        include_directories : include_directories('src'))
    test('state-log', state_log_test, args : [meson.current_build_dir() / 'test-state-log'])

    report_test = executable('report-test',
        'test/report_test.cpp',
        include_directories : include_directories('src'))
    test('report', report_test)

    x11_input_filter_test = executable('x11-input-filter-test',
        'test/x11_input_filter_test.cpp',
        include_directories : include_directories('src'))
    test('x11-input-filter', x11_input_filter_test)

    hud_test = executable('hud-test',
        'test/hud_test.cpp',
        include_directories : include_directories('src'))
    test('hud', hud_test)

    launch_notice_test = executable('launch-notice-test',
        'test/launch_notice_test.cpp',
        include_directories : include_directories('src'))
    test('launch-notice', launch_notice_test)

    program_scope_test = executable('program-scope-test',
        'test/program_scope_test.cpp', 'src/program_scope.cpp', 'src/anticheat_files.cpp', 'src/config_serializer.cpp', 'src/logger.cpp',
        include_directories : include_directories('src'))
    test('program-scope', program_scope_test, args : [meson.current_build_dir() / 'test-program-scope-config'])

    active_profile_test = executable('active-profile-test',
        'test/active_profile_test.cpp', 'src/program_scope.cpp', 'src/anticheat_files.cpp', 'src/config_serializer.cpp', 'src/logger.cpp',
        include_directories : include_directories('src'))
    test('active-profile', active_profile_test,
        args : [meson.current_build_dir() / 'test-active-profile-config'])

    shared_profiles_test = executable('shared-profiles-test',
        'test/shared_profiles_test.cpp', 'src/config_serializer.cpp', 'src/logger.cpp',
        include_directories : include_directories('src'))
    test('shared-profiles', shared_profiles_test,
        args : [meson.current_build_dir() / 'test-shared-profiles'])

    config_folder_move_test = executable('config-folder-move-test',
        'test/config_folder_move_test.cpp', 'src/config_serializer.cpp', 'src/logger.cpp',
        include_directories : include_directories('src'))
    test('config-folder-move', config_folder_move_test,
        args : [meson.current_build_dir() / 'test-config-folder-move'])

    mouse_frame_test = executable('mouse-frame-snapshot-test',
        'test/mouse_frame_snapshot_test.cpp', 'src/mouse_input.cpp',
        include_directories : include_directories('src'),
        dependencies : [x11_dep, xi_dep])
    test('mouse-frame-snapshot', mouse_frame_test)

    wayland_pointer_state_test = executable('wayland-pointer-state-test',
        'test/wayland_pointer_state_test.cpp',
        include_directories : include_directories('src'))
    test('wayland-pointer-state', wayland_pointer_state_test)

    frame_layout_test = executable('frame-layout-test',
        'test/frame_layout_test.cpp',
        include_directories : include_directories('src'))
    test('frame-layout', frame_layout_test)

    desktop_scale_test = executable('desktop-scale-test',
        'test/desktop_scale_test.cpp',
        include_directories : include_directories('src'))
    test('desktop-scale', desktop_scale_test)

    motion_test = executable('motion-test',
        'test/motion_test.cpp',
        include_directories : include_directories('src'))
    test('motion', motion_test)

    chain_rows_test = executable('chain-rows-test',
        'test/chain_rows_test.cpp',
        include_directories : include_directories('src'))
    test('chain-rows', chain_rows_test)

    elide_start_test = executable('elide-start-test',
        'test/elide_start_test.cpp',
        include_directories : include_directories('src'))
    test('elide-start', elide_start_test)

    definition_title_test = executable('definition-title-test',
        'test/definition_title_test.cpp',
        include_directories : include_directories('src'))
    test('definition-title', definition_title_test)

    log_search_test = executable('log-search-test',
        'test/log_search_test.cpp',
        include_directories : include_directories('src'))
    test('log-search', log_search_test)

    frame_generation_place_test = executable('frame-generation-place-test',
        'test/frame_generation_place_test.cpp',
        include_directories : include_directories('src'))
    test('frame-generation-place', frame_generation_place_test)

    shader_test_result_test = executable('shader-test-result-test',
        'test/shader_test_result_test.cpp',
        include_directories : include_directories('src'))
    test('shader-test-result', shader_test_result_test)

    input_blocker_test = executable('input-blocker-test',
        'test/input_blocker_test.cpp', 'src/input_blocker.cpp', 'src/logger.cpp',
        include_directories : include_directories('src'),
        dependencies : [x11_dep])
    test('input-blocker', input_blocker_test)

    input_hook_driver = shared_module('input-hook-driver', 'test/input-hook/driver.c',
        dependencies : [wayland_client_dep])
    input_hook_test = executable('input-hook-test', 'test/input_hook_test.cpp',
        include_directories : include_directories('src', 'test'),
        dependencies : [wayland_client_dep, dl_dep])
    input_hook_env = environment()
    input_hook_env.set('LD_PRELOAD', input_hook_lib.full_path())
    input_hook_env.append('ASAN_OPTIONS', 'verify_asan_link_order=0', separator : ':')
    test('input-hook', input_hook_test,
        args : [input_hook_driver.full_path()],
        env : input_hook_env,
        depends : [input_hook_driver, input_hook_lib])

    summary({
        'wayland-client' : wayland_client_dep.version(),
        'pointer warp event handled' : cpp.has_header_symbol('wayland-client-protocol.h', 'WL_POINTER_WARP_SINCE_VERSION', dependencies : wayland_client_dep),
        'high-resolution scroll handled' : cpp.has_header_symbol('wayland-client-protocol.h', 'WL_POINTER_AXIS_VALUE120_SINCE_VERSION', dependencies : wayland_client_dep),
    }, section : 'Wayland', bool_yn : true)
endif

if get_option('with_json')
    subdir('config')
endif
