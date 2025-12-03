
gstampperformance = shared_module(
  'ampperformance',
  sources: [
    'ampperformance.cpp'
  ],
  include_directories: common_include,
  dependencies: amp_deps + [dependency('cairo')],
  link_with: [ common ],
  gnu_symbol_visibility: 'hidden',
  install: false
)
