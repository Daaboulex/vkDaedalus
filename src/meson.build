subdir('shader')

overlay_src = [
    'overlay/imgui_overlay.cpp',
    'overlay/overlay_renderer.cpp',
    'overlay/view_add_effects.cpp',
    'overlay/view_debug.cpp',
    'overlay/view_hud.cpp',
    'overlay/view_notice.cpp',
    'overlay/view_diagnostics.cpp',
    'overlay/view_latency.cpp',
    'overlay/view_settings.cpp',
    'overlay/view_shader_manager.cpp',
    'overlay/view_shader_test.cpp',
    'overlay/view_main.cpp',
    'overlay/view_frame_generation.cpp',
    'overlay/view_gpus.cpp',
    'overlay/overlay_style.cpp',
    'overlay/frame_draw.cpp',
    'overlay/widgets.cpp',
    'overlay/view_frame.cpp',
    'overlay/ui_map.cpp',
    'overlay/params/field_editor.cpp',
    'overlay/params/fields/field_bool.cpp',
    'overlay/params/fields/field_float.cpp',
    'overlay/params/fields/field_floatvec.cpp',
    'overlay/params/fields/field_int.cpp',
    'overlay/params/fields/field_intvec.cpp',
    'overlay/params/fields/field_uint.cpp',
    'overlay/params/fields/field_uintvec.cpp',
]

vkBasalt_src = [
    'basalt.cpp',
    'anticheat_files.cpp',
    'buffer.cpp',
    'command_buffer.cpp',
    'config.cpp',
    'config_serializer.cpp',
    'program_scope.cpp',
    'settings_manager.cpp',
    'descriptor_set.cpp',
    'gpu_probe.cpp',
    'helper_device.cpp',
    'gpu_chain.cpp',
    'hand_off.cpp',
    'instance_gpus.cpp',
    'effects/effect.cpp',
    'effects/effect_registry.cpp',
    'effects/effect_reshade.cpp',
    'effects/effect_shared_copy.cpp',
    'effects/effect_simple.cpp',
    'effects/effect_transfer.cpp',
    'effects/builtin/builtin_effects.cpp',
    'effects/builtin/effect_cas.cpp',
    'effects/builtin/effect_rcas.cpp',
    'effects/builtin/effect_deband.cpp',
    'effects/builtin/effect_dls.cpp',
    'effects/builtin/effect_fxaa.cpp',
    'effects/builtin/effect_lut.cpp',
    'effects/builtin/effect_smaa.cpp',
    'reshade_parser.cpp',
    'shader_cache.cpp',
    'fake_swapchain.cpp',
    'format.cpp',
    'frame_generation.cpp',
    'game_input_hook.cpp',
    'framebuffer.cpp',
    'graphics_pipeline.cpp',
    'image.cpp',
    'image_view.cpp',
    'keyboard_input.cpp',
    'logger.cpp',
    'logical_swapchain.cpp',
    'lut_cube.cpp',
    'memory.cpp',
    'renderpass.cpp',
    'reshade_uniforms.cpp',
    'sampler.cpp',
    'shader.cpp',
    'stb_image.c',
    'stb_image_resize2.c',
    'util.cpp',
    'vkdispatch.cpp',
]

x11_dep = dependency('x11')
x11_xcb_dep = dependency('x11-xcb')
xi_dep = dependency('xi')
wayland_client_dep = dependency('wayland-client')
xkbcommon_dep = dependency('xkbcommon')
wayland_protocols_dep = dependency('wayland-protocols')
dl_dep = cpp.find_library('dl')
wayland_scanner = find_program('wayland-scanner')

relative_pointer_xml = join_paths(wayland_protocols_dep.get_variable('pkgdatadir'),
    'unstable', 'relative-pointer', 'relative-pointer-unstable-v1.xml')

relative_pointer_client_h = custom_target('relative-pointer-client-header',
    input: relative_pointer_xml,
    output: 'relative-pointer-unstable-v1-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])

relative_pointer_code = custom_target('relative-pointer-code',
    input: relative_pointer_xml,
    output: 'relative-pointer-unstable-v1-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])

xdg_output_xml = join_paths(wayland_protocols_dep.get_variable('pkgdatadir'),
    'unstable', 'xdg-output', 'xdg-output-unstable-v1.xml')

xdg_output_client_h = custom_target('xdg-output-client-header',
    input: xdg_output_xml,
    output: 'xdg-output-unstable-v1-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])

xdg_output_code = custom_target('xdg-output-code',
    input: xdg_output_xml,
    output: 'xdg-output-unstable-v1-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])

conf_paths = configuration_data()
conf_paths.set_quoted('SYSCONFDIR', join_paths(get_option('prefix'), get_option('sysconfdir')))
conf_paths.set_quoted('DATADIR', join_paths(get_option('prefix'), get_option('datadir')))
reshade_include_dir = join_paths(get_option('datadir'), 'vkdaedalus', 'reshade-include')
conf_paths.set_quoted('RESHADE_INCLUDE_DIR', join_paths(get_option('prefix'), reshade_include_dir))
conf_paths.set('VKBASALT_LAYER_API_MAJOR', layer_api_major)
conf_paths.set('VKBASALT_LAYER_API_MINOR', layer_api_minor)
conf_paths.set('VKBASALT_LAYER_API_PATCH', layer_api_patch)

configure_file(output: 'config_paths.hpp',
               configuration : conf_paths)

install_data(reshade_fxh, 'reshade_include/ReShadeUI.fxh', install_dir : reshade_include_dir)

anticheat_rules_section = import('fs').read(anticheat_rules_ini[0]).split('[AntiCheat]\n')[1].split('\n[')[0]
configure_file(input : 'anticheat_rules.hpp.in',
               output : 'anticheat_rules.hpp',
               configuration : {'ANTICHEAT_RULES' : anticheat_rules_section})

# X11/X.h macros such as None clash with other sources in a meson --unity=on build
keyboard_input_x11_lib = static_library('keyboard_input_x11', 'keyboard_input_x11.cpp')
mouse_input_lib = static_library('mouse_input', 'mouse_input.cpp')
input_blocker_lib = static_library('input_blocker', 'input_blocker.cpp',
    dependencies: [dl_dep])

wayland_input_common_lib = static_library('wayland_input_common',
    'wayland_input_common.cpp', 'wayland_display.cpp',
    relative_pointer_code, relative_pointer_client_h, xdg_output_code, xdg_output_client_h,
    dependencies: [wayland_client_dep])
keyboard_input_wayland_lib = static_library('keyboard_input_wayland',
    'keyboard_input_wayland.cpp', relative_pointer_client_h,
    link_with: [wayland_input_common_lib],
    dependencies: [wayland_client_dep, xkbcommon_dep])
mouse_input_wayland_lib = static_library('mouse_input_wayland',
    'mouse_input_wayland.cpp', relative_pointer_client_h,
    link_with: [wayland_input_common_lib],
    dependencies: [wayland_client_dep])

overlay_inc = include_directories('overlay')
effects_inc = include_directories('effects')
effects_builtin_inc = include_directories('effects/builtin')
effects_params_inc = include_directories('effects/params')

font_to_c = subproject('imgui').get_variable('font_to_c')
barlow = subproject('barlow')
overlay_fonts = []
foreach font : [['barlow_regular', 'regular_ttf'], ['barlow_semibold', 'semibold_ttf'], ['barlow_semicondensed_bold', 'semicondensed_bold_ttf']]
    overlay_fonts += custom_target(font[0] + '_font',
        input : barlow.get_variable(font[1]),
        output : font[0] + '_font.h',
        command : [font_to_c, '@INPUT@', font[0]],
        capture : true)
endforeach
install_data(barlow.get_variable('licence'), rename : 'barlow-OFL.txt', install_dir : get_option('datadir') / 'doc' / 'vkdaedalus')

shared_library(meson.project_name().to_lower(),
    vkBasalt_src, overlay_src, shader_include, overlay_fonts,
    link_with: [keyboard_input_x11_lib, mouse_input_lib, input_blocker_lib,
                wayland_input_common_lib, keyboard_input_wayland_lib, mouse_input_wayland_lib],
    include_directories : [vkBasalt_include_path, overlay_inc, effects_inc, effects_builtin_inc, effects_params_inc],
    dependencies : [x11_dep, x11_xcb_dep, xi_dep, wayland_client_dep, xkbcommon_dep, reshade_dep, dl_dep, stb_dep, imgui_dep, imgui_filebrowser_dep, tomlplusplus_dep],
    gnu_symbol_visibility: 'hidden',
    install : true,
    install_dir : lib_dir)

test_shaders_exe = executable('vkdaedalus-test-shaders',
    '../tools/test_shaders.cpp', 'shader_cache.cpp', 'logger.cpp',
    include_directories : vkBasalt_include_path,
    dependencies : [reshade_dep, dl_dep],
    install : true)

executable('vkdaedalus-profile',
    '../tools/profile.cpp', 'config_serializer.cpp', 'logger.cpp',
    include_directories : vkBasalt_include_path,
    install : true)

executable('vkdaedalus-frame-order',
    '../tools/frame_order.cpp', 'config_serializer.cpp', 'logger.cpp', 'frame_generation.cpp',
    include_directories : vkBasalt_include_path,
    dependencies : [tomlplusplus_dep, dl_dep, dependency('vulkan')],
    install : true)

input_hook_lib = shared_library('vkdaedalus-input-hook',
    'input_hook.cpp',
    dependencies: [wayland_client_dep.partial_dependency(compile_args : true, includes : true),
                   dependency('xcb').partial_dependency(compile_args : true, includes : true), dl_dep],
    gnu_symbol_visibility: 'hidden',
    install : true,
    install_dir : lib_dir)

configure_file(input : '../vkdaedalus-run.sh',
    output : 'vkdaedalus-run',
    configuration : {
        'input_hook' : join_paths(get_option('prefix'), lib_dir, 'libvkdaedalus-input-hook.so'),
        'frame_order' : join_paths(get_option('prefix'), get_option('bindir'), 'vkdaedalus-frame-order'),
        'order_layers' : join_paths(get_option('prefix'), get_option('datadir'), 'vkdaedalus', 'order-layers'),
    },
    install_dir : get_option('bindir'),
    install_mode : 'rwxr-xr-x')
