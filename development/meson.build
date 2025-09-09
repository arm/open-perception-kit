project('amp_elements', ['c', 'cpp'],
  version: '0.1.0',
  default_options: [
    'c_std=c11',
    'cpp_std=c++20',
    'warning_level=2',
    'b_lto=false', # true for release
  ]
)

# dependencies
gst_dep        = dependency('gstreamer-1.0', required: true)
gstbase_dep    = dependency('gstreamer-base-1.0', required: true)
glib_dep       = dependency('glib-2.0', required: true)
gobject_dep    = dependency('gobject-2.0', required: true)

amp_deps = [gst_dep, gstbase_dep, glib_dep, gobject_dep]

inc_root = include_directories('common/include')

# add each element subdir
subdir('elements/ampdummy')
