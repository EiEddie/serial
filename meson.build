project('serial', 'c')

if get_option('buildtype') == 'debug'
    add_project_arguments('-D_DEBUG_', language: 'c')
    message('add macro \'_DEBUG_\'')

    libuv = dependency('libuv', required: true)
elif get_option('buildtype') == 'release'
    libuv = dependency('libuv', static: true, required: true)
    message('static link \'libuv\'')
endif

srcs = ['packet.h', 'main.c']
executable('serial', srcs, dependencies: libuv)
