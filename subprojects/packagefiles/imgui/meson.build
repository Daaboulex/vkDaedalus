project('imgui', 'cpp', default_options : ['cpp_std=c++20'])

imgui_args = [
    '-DIMGUI_DISABLE_DEMO_WINDOWS',
    '-DIMGUI_DISABLE_DEBUG_TOOLS',
    '-DIMGUI_DISABLE_OBSOLETE_FUNCTIONS',
    '-DIMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS',
    '-DIMGUI_ENABLE_TEST_ENGINE',
]
imgui_inc = include_directories('.')

imgui_lib = static_library('imgui',
    'imgui.cpp', 'imgui_draw.cpp', 'imgui_tables.cpp', 'imgui_widgets.cpp',
    include_directories : imgui_inc,
    cpp_args : imgui_args,
    gnu_symbol_visibility : 'hidden',
    pic : true)

font_to_c = executable('binary-to-compressed-c', 'misc/fonts/binary_to_compressed_c.cpp', native : true)

imgui_dep = declare_dependency(link_with : imgui_lib, include_directories : imgui_inc, compile_args : imgui_args)
