
gstampdummy = shared_module(
  'gstampdummy',                   
  sources: [
    'ampdummy.cpp'
  ],
  include_directories: inc_root,
  dependencies: amp_deps,
  gnu_symbol_visibility: 'hidden', # good hygiene
  install: false                   # dev flow: keep in build dir; use GST_PLUGIN_PATH
)
