project('reshade-fx', 'cpp', default_options : ['cpp_std=c++20'])

spirv_headers_dep = dependency('SPIRV-Headers')
spirv_inc = include_directories(spirv_headers_dep.get_variable(pkgconfig : 'includedir') / 'spirv' / 'unified1')

reshade_lib = static_library('reshade-fx',
    'source/effect_codegen_spirv.cpp',
    'source/effect_expression.cpp',
    'source/effect_lexer.cpp',
    'source/effect_parser_exp.cpp',
    'source/effect_parser_stmt.cpp',
    'source/effect_preprocessor.cpp',
    'source/effect_symbol_table.cpp',
    include_directories : [include_directories('source'), spirv_inc],
    cpp_args : meson.get_compiler('cpp').get_supported_arguments(['-Wno-unknown-pragmas']),
    gnu_symbol_visibility : 'hidden',
    pic : true)

subdir('reshade')

reshade_dep = declare_dependency(link_with : reshade_lib, include_directories : include_directories('.'))
