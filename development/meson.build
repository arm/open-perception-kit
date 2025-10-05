project('amp_elements', ['c', 'cpp'],
  version: '0.1.0',
  default_options: [
    'c_std=c11',
    'cpp_std=c++20',
    'warning_level=2',
    'b_lto=false', # true for release
  ]
)

add_project_arguments('-Wno-unused-parameter', language: 'c')
add_project_arguments('-Wno-unused-parameter', language: 'cpp')
add_project_arguments('-Wno-unused-function', language: 'c')
add_project_arguments('-Wno-unused-function', language: 'cpp')

# dependencies
gst_dep        = dependency('gstreamer-1.0', required: true)
gstbase_dep    = dependency('gstreamer-base-1.0', required: true)
gstv_dep  = dependency('gstreamer-video-1.0')
glib_dep       = dependency('glib-2.0', required: true)
gobject_dep    = dependency('gobject-2.0', required: true)

onnx_include = include_directories('../deps/onnxruntime/include')
onnx_libdir = meson.project_source_root() / '../deps/onnxruntime/lib'
message('ONNX include dir: deps/onnxruntime/include')
message('ONNX lib dir: ' + onnx_libdir)

onnx_dep = declare_dependency(
  include_directories: onnx_include,
  link_args: ['-L' + onnx_libdir, '-lonnxruntime']
)

amp_deps = [gst_dep, gstbase_dep, gstv_dep, glib_dep, gobject_dep, onnx_dep]

inc_root = include_directories('common')

# add each element subdir
subdir('common')
subdir('elements/ampdummy')
subdir('elements/ampinfer')
