project('webui-rdp-client', 'cpp', 'c',
  version: '0.1.0',
  default_options: [
    'cpp_std=c++17',
    'c_std=c11',
    'warning_level=2',
    'buildtype=debugoptimized',
  ]
)

# Platform detection
host_system = host_machine.system()
is_windows = host_system == 'windows'
is_linux = host_system == 'linux'
is_macos = host_system == 'darwin'

# Compiler setup
cpp = meson.get_compiler('cpp')
cc = meson.get_compiler('c')

# Common dependencies
thread_dep = dependency('threads')

# ============================================================================
# WebUI Dependency (fetched via wrap)
# ============================================================================
webui_dep = dependency('webui', fallback: ['webui', 'webui_dep'])

# ============================================================================
# FreeRDP Dependency (from submodule - we build the client library)
# ============================================================================
# FreeRDP has its own CMake build, so we use cmake module
cmake = import('cmake')

freerdp_opts = cmake.subproject_options()
freerdp_opts.add_cmake_defines({
  'BUILD_SHARED_LIBS': false,
  'WITH_CLIENT': true,
  'WITH_SERVER': false,
  'WITH_SAMPLE': false,
  'WITH_PLATFORM_SERVER': false,
  'WITH_PROXY': false,
  'WITH_SHADOW': false,
  'CHANNEL_URBDRC': false,
  'WITH_MANPAGES': false,
  'WITH_GSSAPI': false,
  'WITH_PKCS11': false,
  'WITH_CLIENT_SDL': false,
  'WITH_CLIENT_WAYLAND': false,
  'WITH_WAYLAND': false,
  # Disable features that bring in extra dependencies
  'WITH_FUSE': false,
  'CHANNEL_REMDESK': false,
})

# Try to find system FreeRDP first, fallback to subproject
freerdp_dep = dependency('freerdp3', required: false)
freerdp_client_dep = dependency('freerdp-client3', required: false)
winpr_dep = dependency('winpr3', required: false)

# X11 FreeRDP client library (contains RdpClientEntry for popup windows)
xfreerdp_client_dep = dependency('xfreerdp-client', required: false)

if not freerdp_dep.found() or not freerdp_client_dep.found()
  # Build from submodule using CMake
  message('Building FreeRDP from submodule...')
  freerdp_sub = cmake.subproject('freerdp', options: freerdp_opts)
  freerdp_dep = freerdp_sub.dependency('freerdp')
  freerdp_client_dep = freerdp_sub.dependency('freerdp-client')
  winpr_dep = freerdp_sub.dependency('winpr')
  # Get the X11 client library for direct integration
  xfreerdp_client_dep = freerdp_sub.dependency('xfreerdp_client')
endif

# ============================================================================
# Platform-specific dependencies
# ============================================================================
platform_deps = []

if is_linux
  # X11 for window management
  x11_dep = dependency('x11', required: false)
  if x11_dep.found()
    platform_deps += x11_dep
  endif
  
  # Audio support (ALSA)
  alsa_dep = dependency('alsa', required: false)
  if alsa_dep.found()
    platform_deps += alsa_dep
  endif
  
  # Printing support (CUPS)
  cups_dep = dependency('cups', required: false)
  if cups_dep.found()
    platform_deps += cups_dep
  endif
  
  # Additional X11 extensions
  xi_dep = dependency('xi', required: false)
  if xi_dep.found()
    platform_deps += xi_dep
  endif
  
  xrandr_dep = dependency('xrandr', required: false)
  if xrandr_dep.found()
    platform_deps += xrandr_dep
  endif
  
  xrender_dep = dependency('xrender', required: false)
  if xrender_dep.found()
    platform_deps += xrender_dep
  endif
  
  xfixes_dep = dependency('xfixes', required: false)
  if xfixes_dep.found()
    platform_deps += xfixes_dep
  endif
  
  xcursor_dep = dependency('xcursor', required: false)
  if xcursor_dep.found()
    platform_deps += xcursor_dep
  endif
  
  xinerama_dep = dependency('xinerama', required: false)
  if xinerama_dep.found()
    platform_deps += xinerama_dep
  endif
  
elif is_windows
  platform_deps += cpp.find_library('ws2_32')
  platform_deps += cpp.find_library('user32')
  platform_deps += cpp.find_library('shell32')
endif

# ============================================================================
# Source files
# ============================================================================
sources = files(
  'src/main.cpp',
  'src/rdp_launcher.cpp',
  'src/config_manager.cpp',
)

# ============================================================================
# Include directories
# ============================================================================
inc_dirs = include_directories('src')

# ============================================================================
# Install UI assets
# ============================================================================
install_subdir('src/ui', install_dir: get_option('datadir') / meson.project_name())

# For development, copy UI files to build directory
ui_files = files(
  'src/ui/index.html',
  'src/ui/styles.css',
  'src/ui/app.js',
)

# ============================================================================
# Main executable
# ============================================================================
executable('webui-rdp-client',
  sources,
  include_directories: inc_dirs,
  dependencies: [
    thread_dep,
    webui_dep,
    xfreerdp_client_dep,  # X11 FreeRDP client for popup windows
    freerdp_dep,
    freerdp_client_dep,
    winpr_dep,
    platform_deps,
  ],
  install: true,
  # Embed the UI path for runtime
  cpp_args: [
    '-DUI_PATH="' + meson.current_source_dir() / 'src/ui"',
  ],
)

# Summary
summary({
  'Platform': host_system,
  'Build type': get_option('buildtype'),
  'C++ standard': get_option('cpp_std'),
}, section: 'Configuration')
