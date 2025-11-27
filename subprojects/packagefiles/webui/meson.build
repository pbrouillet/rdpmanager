# Meson build file for WebUI library
# This is a wrap overlay for the webui-dev/webui project

project('webui', 'c', 'cpp',
  version: '2.5.0',
  default_options: ['c_std=c11', 'cpp_std=c++17'],
)

cc = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')

# WebUI is primarily a header-only library with a small source component
webui_inc = include_directories('include')

# Platform-specific settings
host_system = host_machine.system()

c_args = []
link_args = []
deps = []

if host_system == 'windows'
  c_args += ['-DWEBUI_STATIC', '-DWIN32']
  deps += cpp.find_library('ws2_32')
  deps += cpp.find_library('user32')
  deps += cpp.find_library('shell32')
  deps += cpp.find_library('ole32')
elif host_system == 'linux'
  c_args += ['-DWEBUI_STATIC', '-DWEBUI_LOG', '-D_GNU_SOURCE', '-DNO_SSL', '-DNO_CGI', '-DUSE_WEBSOCKET']
  deps += dependency('threads')
elif host_system == 'darwin'
  c_args += ['-DWEBUI_STATIC']
  deps += dependency('threads')
endif

# Build the static library
webui_lib = static_library('webui',
  'src/webui.c',
  'src/civetweb/civetweb.c',
  include_directories: webui_inc,
  c_args: c_args,
  dependencies: deps,
)

# Export as dependency
webui_dep = declare_dependency(
  include_directories: webui_inc,
  link_with: webui_lib,
  dependencies: deps,
  compile_args: c_args,
)
