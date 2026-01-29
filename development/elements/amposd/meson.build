
gstamposd = shared_module(
  'amposd',
  sources: [
    'amposd.cpp'
  ],
  include_directories: common_include,
  dependencies: amp_deps + [dependency('cairo')],
  link_with: [ common ],
  gnu_symbol_visibility: 'hidden',
  install: false
)
