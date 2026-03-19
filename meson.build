project('webui-rdp-client', 'cpp', 'c',
  version: '0.1.0',
  default_options: [
    'cpp_std=c++23',
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
  'WITH_MANPAGES': false,
  'WITH_GSSAPI': false,
  'WITH_KRB5': false,
  'WITH_PKCS11': false,
  'WITH_CLIENT_SDL': false,
  'WITH_CLIENT_WAYLAND': false,
  'WITH_WAYLAND': false,
  # Disable features that bring in extra dependencies
  'WITH_FUSE': false,
  'CHANNEL_REMDESK': false,
  
  # Enable Azure AD authentication
  'WITH_AAD': true,
  
  # Enable H264 video codec support (via FFmpeg)
  'WITH_FFMPEG': true,
  'WITH_VIDEO_FFMPEG': true,
  'WITH_DSP_FFMPEG': true,
  # OpenH264 is optional - system may not have it installed
  # FFmpeg with libx264 provides H264 support
  'WITH_OPENH264': false,
  
  # Use built-in Unicode converter (avoids ~35MB ICU runtime dependency)
  'WITH_UNICODE_BUILTIN': true,
  
  # Enable compression
  'WITH_BULK_COMPRESSION': true,
  
  # Enable clipboard redirection (already ON by default, but explicit)
  'CHANNEL_CLIPRDR': true,
  'CHANNEL_CLIPRDR_CLIENT': true,
  
  # Enable drive/folder sharing
  'CHANNEL_DRIVE': true,
  'CHANNEL_DRIVE_CLIENT': true,
  
  # Enable camera/video capture redirection
  # Note: Requires V4L2 and libusb at runtime
  'CHANNEL_RDPECAM': true,
  'CHANNEL_RDPECAM_CLIENT': true,
  
  # USB device forwarding - disabled due to complex static linking requirements
  # for urbdrc_common library. Enable if building with shared libraries or
  # using system FreeRDP installation.
  'CHANNEL_URBDRC': false,
  'CHANNEL_URBDRC_CLIENT': false,
  
  # Enable device redirection (printers, serial, parallel, smartcard)
  'CHANNEL_RDPDR': true,
  'CHANNEL_RDPDR_CLIENT': true,
  
  # Enable dynamic virtual channels (required for many dynamic channels)
  'CHANNEL_DRDYNVC': true,
  'CHANNEL_DRDYNVC_CLIENT': true,
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

# JSON parsing library (jansson)
jansson_dep = dependency('jansson', required: true)
sqlite_dep = dependency('sqlite3', required: true)

# HTTP client library (libcurl) - for WVD feed discovery
curl_dep = dependency('libcurl', required: true)

# XML parsing library (pugixml) - for WVD feed discovery XML responses
pugixml_dep = dependency('pugixml', required: true)

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
  
  # libusb for USB device and camera redirection
  libusb_dep = dependency('libusb-1.0', required: false)
  if libusb_dep.found()
    platform_deps += libusb_dep
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
  'src/gui/main_window.cpp',
  'src/gui/aad_auth_handler.cpp',
  'src/rdp_launcher.cpp',
  'src/config_manager.cpp',
  'src/rdp_file_parser.cpp',
  'src/dialog_manager.cpp',
  'src/js_handlers.cpp',
  'src/feed_discovery.cpp',
)

# ============================================================================
# Include directories
# ============================================================================
inc_dirs = include_directories('src', 'src/gui')

# ============================================================================
# Build React UI (auto-builds the TypeScript/Fluent UI frontend)
# ============================================================================
npm = find_program('npm', required: false)
node = find_program('node', required: false)
bash_prog = find_program('bash')

ui_react_src = meson.current_source_dir() / 'src' / 'ui-react'
ui_react_dist = meson.current_build_dir() / 'ui-dist'

if npm.found() and node.found()
  ui_build = custom_target('react-ui',
    output: 'ui-dist',
    command: [
      bash_prog,
      meson.current_source_dir() / 'src' / 'ui-react' / 'build-ui.sh',
      ui_react_src,
      ui_react_dist,
    ],
    console: true,
    build_always_stale: true,
  )
  ui_path = ui_react_dist
  message('React UI will be built from src/ui-react/')
else
  ui_build = []
  ui_path = meson.current_source_dir() / 'src' / 'ui'
  message('npm/node not found — using plain HTML UI from src/ui/')
endif

# ============================================================================
# Install UI assets
# ============================================================================
embed_ui = get_option('embed_ui')
python = find_program('python3', required: embed_ui)

if embed_ui and npm.found() and node.found()
  # VFS mode: generate a C++ header with all UI assets embedded as byte arrays.
  # This produces a single-binary deployment where the executable serves files
  # from compiled-in data instead of reading them from disk.
  #
  # The custom_target 'output' is relative to the build dir.
  # We write the generated header into the build dir, then add the build dir
  # to the compiler include path so `#include "embedded_ui.hpp"` finds it.
  vfs_gen = custom_target('vfs-header',
    output: 'embedded_ui.hpp',
    depends: ui_build,
    command: [
      python,
      meson.current_source_dir() / 'scripts' / 'generate_vfs.py',
      ui_react_dist,
      '@OUTPUT@',
    ],
    console: true,
  )
  message('Embedded UI mode: assets will be compiled into the executable')
elif npm.found() and node.found()
  vfs_gen = []
  message('Install the React UI manually: cp -r ' + ui_react_dist + ' to your prefix')
else
  vfs_gen = []
  install_subdir('src/ui', install_dir: get_option('datadir') / meson.project_name())
endif

# ============================================================================
# Transitive dependencies needed when statically linking FreeRDP
# ============================================================================
zstd_dep = dependency('libzstd', required: false)
xcb_dep = dependency('xcb', required: false)

# ============================================================================
# Main executable
# ============================================================================

# Build the cpp_args list based on configuration
app_cpp_args = [
  '-DUI_PATH="' + ui_path + '"',
]
if embed_ui
  app_cpp_args += '-DEMBED_UI'
endif
if get_option('verbose_secrets')
  app_cpp_args += '-DVERBOSE_SECRETS'
endif

# Include directories — add the build dir for generated headers (embedded_ui.hpp)
app_inc_dirs = [inc_dirs]

# Source files — include VFS generated header as a dependency so meson builds it first
app_extra_sources = []
if embed_ui and npm.found() and node.found()
  app_extra_sources += vfs_gen
endif

# Windows icon resource — embeds appicon.ico into the .exe
if is_windows
  windows_mod = import('windows')
  app_extra_sources += windows_mod.compile_resources(
    files('src' / 'app.rc'),
    include_directories: include_directories('.'),
  )
endif

executable('webui-rdp-client',
  sources, app_extra_sources,
  include_directories: app_inc_dirs,
  dependencies: [
    thread_dep,
    webui_dep,
    jansson_dep,
    sqlite_dep,
    curl_dep,
    pugixml_dep,
    xfreerdp_client_dep,  # X11 FreeRDP client for popup windows
    freerdp_dep,
    freerdp_client_dep,
    winpr_dep,
    platform_deps,
    zstd_dep,
    xcb_dep,
  ],
  link_depends: ui_build,
  install: true,
  cpp_args: app_cpp_args,
)

# Summary
summary({
  'Platform': host_system,
  'Build type': get_option('buildtype'),
  'C++ standard': get_option('cpp_std'),
  'Embed UI': embed_ui,
}, section: 'Configuration')
