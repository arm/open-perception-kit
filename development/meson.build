project('pek_elements', ['c', 'cpp'],
  version: '0.1.0',
  default_options: [
    'c_std=c11',
    'cpp_std=c++20',
    'warning_level=2',
    'b_lto=false', # true for release
  ]
)

add_project_arguments('-Wno-unused-variable', language: ['c', 'cpp'])
add_project_arguments('-Wno-unused-parameter', language: ['c', 'cpp'])
add_project_arguments('-Wno-unused-function', language: ['c', 'cpp'])
add_project_arguments('-Wno-unused-but-set-variable', language: ['c', 'cpp'])

# -------- Core deps (always)
stb_dep = dependency('stb', fallback: ['stb', 'stb_dep'])
gst_dep      = dependency('gstreamer-1.0',        required: true)
gstbase_dep  = dependency('gstreamer-base-1.0',   required: true)
gstv_dep     = dependency('gstreamer-video-1.0',  required: true)
gsta_dep     = dependency('gstreamer-audio-1.0',  required: true)
gstalloc_dep = dependency('gstreamer-allocators-1.0', required: true)

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

fmt_dep = dependency('fmt', fallback : ['fmt', 'fmt_dep'], required : true)

nlohmann_dep = dependency('nlohmann_json',
                      required : true,
                      fallback : ['nlohmann_json', 'nlohmann_json_dep'])

magic_enum_dep = dependency(
  'magic_enum',
  required : true,
  fallback : ['magic_enum', 'magic_enum_dep']
)

httplib_dep = dependency(
  'cpp-httplib',
  required : true,
  fallback : ['cpp-httplib', 'cpp_httplib_dep']
)

tl_expected_dep = dependency('tl-expected', required : true)

web_server_enabled = soup_dep.found() and json_dep.found()

# -------- ONNX
fs = import('fs')

onnx_root = ''
onnx_candidates = []

onnx_root_env = run_command('sh', '-c', 'printf %s "$PEK_ONNXRUNTIME_ROOT"', check: false).stdout().strip()
if onnx_root_env != ''
  onnx_candidates += [onnx_root_env]
endif

onnx_candidates += [
  '/opt/pek-deps/onnxruntime',
  meson.project_source_root() / '../deps/onnxruntime',
  meson.project_source_root() / '../onnxruntime-linux-aarch64-1.24.4',
]

foreach candidate : onnx_candidates
  if fs.is_dir(candidate / 'include') and fs.is_dir(candidate / 'lib')
    onnx_root = candidate
    break
  endif
endforeach

if onnx_root == ''
  error('ONNX Runtime not found. Set PEK_ONNXRUNTIME_ROOT or provide include/ and lib/ under one of: ' + ', '.join(onnx_candidates))
endif

message('Using ONNX Runtime from: ' + onnx_root)

onnx_libdir  = onnx_root / 'lib'
onnx_dep = declare_dependency(
  compile_args: ['-I' + (onnx_root / 'include')],
  link_args: ['-L' + onnx_libdir, '-lonnxruntime', '-Wl,--no-undefined']
)

# -------- modelfetch C++ SDK
modelfetch_root = ''
modelfetch_candidates = []

modelfetch_root_env = run_command('sh', '-c', 'printf %s "$PEK_MODELFETCH_ROOT"', check: false).stdout().strip()
if modelfetch_root_env != ''
  modelfetch_candidates += [modelfetch_root_env]
endif

modelfetch_candidates += [
  '/opt/pek-deps/modelfetch',
  meson.project_source_root() / '../deps/modelfetch',
]

foreach candidate : modelfetch_candidates
  if fs.is_file(candidate / 'include/modelfetch/modelfetch.hpp') and fs.is_file(candidate / 'lib/libmodelfetch_c.so')
    modelfetch_root = candidate
    break
  endif
endforeach

if modelfetch_root == ''
  error('modelfetch C++ SDK not found. Set PEK_MODELFETCH_ROOT or provide include/ and lib/ under one of: ' + ', '.join(modelfetch_candidates))
endif

message('Using modelfetch C++ SDK from: ' + modelfetch_root)

modelfetch_libdir = modelfetch_root / 'lib'
modelfetch_cpp_headers_dep = declare_dependency(
  compile_args: ['-I' + (modelfetch_root / 'include')]
)
modelfetch_dep = declare_dependency(
  dependencies: [modelfetch_cpp_headers_dep],
  link_args: ['-L' + modelfetch_libdir, '-lmodelfetch_c', '-Wl,--no-undefined']
)

# -------- Exported defs + deps for subdirs
pek_defs = ['-DPEK_WEB_MODE=1']  # Web mode always compiled
if web_server_enabled
  pek_defs += ['-DPEK_WEB_SERVER=1']
  if use_soup2
    pek_defs += ['-DUSE_SOUP2=1']
  else
    pek_defs += ['-DUSE_SOUP3=1']
  endif
else
  pek_defs += ['-DPEK_WEB_SERVER=0']  # fallback to file-based/manual signaling
  warning('libsoup and/or json-glib not found — building peksink without embedded HTTP/WS server (manual signaling mode).')
endif
add_project_arguments(pek_defs, language: 'cpp')

pek_deps = [
  stb_dep,
  gst_dep, gstbase_dep, gstv_dep, gsta_dep, gstalloc_dep,
  glib_dep, gobject_dep,
  webrtc_dep, sdp_dep,
  fmt_dep, nlohmann_dep, nlohmann_dep, magic_enum_dep, tl_expected_dep, httplib_dep
]

if web_server_enabled
  pek_deps += [ soup_dep, json_dep ]
endif

# Common include(s)
common_include = include_directories('common')

# gtest dependencies
cmake = import('cmake')

opt = cmake.subproject_options()
opt.add_cmake_defines({
  'BUILD_GMOCK': false,
  'INSTALL_GTEST': false,
})

gtest_cmake = cmake.subproject('gtest', options : opt)

gtest_dep      = gtest_cmake.dependency('gtest')
gtest_main_dep = gtest_cmake.dependency('gtest_main')

# Subdirs
subdir('common')
subdir('runtime')
subdir('elements/pekinfer')
subdir('ops-onnx')
subdir('ops-std')
subdir('elements/pektracker')
subdir('elements/pekperformance')
subdir('elements/pekosd')
subdir('elements/peksink')
subdir('elements/pekcomm')
subdir('pek-menu')

# Optional subdirs decision tree is inside the subdir
subdir('ops-hailort')
subdir('ops-executorch')
subdir('ops-ncnn')

if get_option('tests')
  subdir('tests')
endif
