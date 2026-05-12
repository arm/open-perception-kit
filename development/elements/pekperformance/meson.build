
gstpekperformance = shared_module(
  'pekperformance',
  sources: [
    'pekperformance.cpp'
  ],
  include_directories: common_include,
  dependencies: pek_deps + [dependency('cairo')],
  link_with: [ common ],
  gnu_symbol_visibility: 'hidden',
  install: false
)
