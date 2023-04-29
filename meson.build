project('cdba', 'c')

cc = meson.get_compiler('c')

ftdi_dep = dependency('libftdi1', required: false)
ftdi_found = ftdi_dep.found()

if not ftdi_found
        ftdi_dep = dependency('libftdi', required: true)
endif

server_deps = [
        cc.find_library('yaml', dirs: ['/usr/lib', '/usr/include']),
        dependency('libudev'),
        ftdi_dep,
]

inc = [
        include_directories('include'),
]

client_src = [
        'cdba.c',
        'lib/circ_buf.c',
]

server_src = [
        'cdba-server.c',
        'lib/cdb_assist.c',
        'lib/circ_buf.c',
        'lib/conmux.c',
        'lib/device.c',
        'lib/device_parser.c',
        'lib/fastboot.c',
        'lib/alpaca.c',
        'lib/external.c',
        'lib/ftdi-gpio.c',
        'lib/console.c',
        'lib/qcomlt_dbg.c',
]

executable('cdba', client_src,
        include_directories: inc,
        install: true)

executable('cdba-server', server_src,
        dependencies: server_deps,
        include_directories: inc,
        install: true)
