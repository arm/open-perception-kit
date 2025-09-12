
gstampinfer = shared_module(
  'gstampinfer',                   
  sources: [
    'ampinfer.cpp',
    'yolov8n-int8.cpp'
  ],
  include_directories: inc_root,
  dependencies: amp_deps,
  gnu_symbol_visibility: 'hidden', 
  install: false
)
