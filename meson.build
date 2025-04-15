project('serial', 'c')

if get_option('buildtype') == 'debug'
    add_project_arguments('-D_DEBUG_', language: 'c')
endif

srcs = ['packet.h', 'main.c']
libuv = dependency('libuv')
executable('serial', srcs, dependencies: libuv)
