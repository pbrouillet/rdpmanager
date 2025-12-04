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
  'WITH_MANPAGES': false,
  'WITH_GSSAPI': false,
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
)

# ============================================================================
# Include directories
# ============================================================================
inc_dirs = include_directories('src', 'src/gui')

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
    jansson_dep,
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
