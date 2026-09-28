
gstopkosd = shared_module(
  'opkosd',
  sources: [
    'DebugOverlayRenderer.cpp',
    'opkosd.cpp'
  ],
  include_directories: common_include,
  dependencies: opk_deps,
  link_with: [ common ],
  gnu_symbol_visibility: 'hidden',
  install: release_package,
  install_dir: get_option('libdir') / 'gstreamer-1.0',
  install_rpath: '$ORIGIN/../opk',
)
