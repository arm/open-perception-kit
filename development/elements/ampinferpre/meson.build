
ampinferpre = shared_module(
  'ampinferpre',                   
  sources: [
    'ampinferpre.cpp'
  ],
#  include_directories: [common_include, uniflow_include],
  include_directories: [common_include],
  dependencies: [amp_deps ], 
  gnu_symbol_visibility: 'hidden', 
  link_with: [ common ],
  install: false
)

