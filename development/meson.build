project('amp_elements', ['c', 'cpp'],
  version: '0.1.0',
  default_options: [
    'c_std=c11',
    'cpp_std=c++20',
    'warning_level=2',
    'b_lto=false', # true for release
  ]
)

add_project_arguments('-Wno-unused-variable', language: 'c')
add_project_arguments('-Wno-unused-variable', language: 'cpp')
add_project_arguments('-Wno-unused-parameter', language: 'c')
add_project_arguments('-Wno-unused-parameter', language: 'cpp')
add_project_arguments('-Wno-unused-function', language: 'c')
add_project_arguments('-Wno-unused-function', language: 'cpp')
add_project_arguments('-Wno-unused-but-set-variable', language: 'cpp')
add_project_arguments('-Wno-unused-but-set-variable', language: 'c')

# -------- Core deps (always)
gst_dep      = dependency('gstreamer-1.0',        required: true)
gstbase_dep  = dependency('gstreamer-base-1.0',   required: true)
gstv_dep     = dependency('gstreamer-video-1.0',  required: true)
gsta_dep     = dependency('gstreamer-audio-1.0',  required: true)

glib_dep     = dependency('glib-2.0',             required: true)
gobject_dep  = dependency('gobject-2.0',          required: true)

# -------- Web deps (Web output ALWAYS ON)
# Require WebRTC + SDP (browser playback via WebRTC is mandatory)
webrtc_dep   = dependency('gstreamer-webrtc-1.0', required: true)
sdp_dep      = dependency('gstreamer-sdp-1.0',    required: true)

# Try to enable embedded HTTP/WS server (optional)
# Prefer libsoup-3.0; fall back to 2.4. Also need json-glib.
soup_dep = dependency('libsoup-3.0', required: false)
use_soup2 = false
if not soup_dep.found()
  soup_dep = dependency('libsoup-2.4', required: false)
  use_soup2 = soup_dep.found()
endif
json_dep = dependency('json-glib-1.0', required: false)

web_server_enabled = soup_dep.found() and json_dep.found()

# -------- ONNX + uniflow (as you had)
onnx_include = include_directories('../deps/onnxruntime/include')
onnx_libdir  = meson.project_source_root() / '../deps/onnxruntime/lib'
onnx_dep = declare_dependency(
  include_directories: '../deps/onnxruntime/include',
  link_args: ['-L' + onnx_libdir, '-lonnxruntime', '-Wl,--no-undefined']
)
uniflow_dep = dependency('uniflow', fallback: ['uniflow', 'uniflow_dep'])

# -------- Exported defs + deps for subdirs
amp_defs = ['-DAMP_WEB_MODE=1']  # Web mode always compiled
if web_server_enabled
  amp_defs += ['-DAMP_WEB_SERVER=1']
  if use_soup2
    amp_defs += ['-DUSE_SOUP2=1']
  else
    amp_defs += ['-DUSE_SOUP3=1']
  endif
else
  amp_defs += ['-DAMP_WEB_SERVER=0']  # fallback to file-based/manual signaling
  warning('libsoup and/or json-glib not found — building ampsink without embedded HTTP/WS server (manual signaling mode).')
endif
add_project_arguments(amp_defs, language: 'cpp')

amp_deps = [
  gst_dep, gstbase_dep, gstv_dep, gsta_dep,
  glib_dep, gobject_dep,
  webrtc_dep, sdp_dep,
  onnx_dep, uniflow_dep,
]

if web_server_enabled
  amp_deps += [ soup_dep, json_dep ]
endif

# Common include(s)
common_include = include_directories('common')

# Subdirs
subdir('common')
subdir('elements/ampinfer')
subdir('elements/ampperformance')
subdir('elements/ampsink')

if get_option('tests')
  subdir('tests')
endif