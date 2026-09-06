
gstpekosd = shared_module(
  'pekosd',
  sources: [
    'RasterOsd.cpp',
    'pekosd.cpp'
  ],
  include_directories: common_include,
  dependencies: pek_deps + [dependency('cairo')],
  link_with: [ common ],
  gnu_symbol_visibility: 'hidden',
  install: release_package,
  install_dir: 'lib/gstreamer-1.0',
  install_rpath: '$ORIGIN/../pek',
)
