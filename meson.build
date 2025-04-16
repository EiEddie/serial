project('serial', 'c')

if get_option('buildtype') == 'debug'
    add_project_arguments('-D_DEBUG_', language: 'c')
    message('add macro \'_DEBUG_\'')

    libuv_dep = dependency('libuv', required: true)
elif get_option('buildtype') == 'release'
    libuv_dep = dependency('libuv', static: true, required: true)
    message('static link \'libuv\'')
endif

inc = include_directories('include')
src = ['src/serial.c']
lib = library('serial',
              sources: src,
              include_directories: inc,
              dependencies: libuv_dep)
lib_dep = declare_dependency(include_directories: inc,
                             link_with: lib)

#executable('serial', src,
#           include_directories: inc,
#           dependencies: libuv_dep)
