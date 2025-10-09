
gstampinfer = shared_module(
  'ampinfer',                   
  sources: [
    'ampinfer.cpp',
    'yolov8n-int8.cpp'
  ],
  include_directories: common_include,
  dependencies: amp_deps,
  gnu_symbol_visibility: 'hidden',
  install: false
)
