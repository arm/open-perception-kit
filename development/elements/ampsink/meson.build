
asio_dep = dependency('asio', required: true,
                             fallback: ['asio', 'asio_dep'])
websocketpp_dep = dependency('websocketpp', required: true,
                             fallback: ['websocketpp', 'websocketpp_dep'])
openssl_dep = dependency('openssl', required : true)

ampsink_static_files_dir = meson.project_source_root() / 'web/content'
ampsink_cpp_args = [
  '-DAMP_DEFAULT_STATIC_FILES_LOCATION="' + ampsink_static_files_dir + '"'
]

ampinferpre = shared_module(
  'ampsink',                   
  sources: [
    'ampsink.cpp',
    'http_server.cpp',
    'webrtc_ws.cpp',
    'ctrl_ws.cpp',
    'utils.cpp',
    'model_reg.cpp'
  ],
  include_directories: [common_include],
  cpp_args: ampsink_cpp_args,
  dependencies: [amp_deps, asio_dep, websocketpp_dep, openssl_dep], 
  gnu_symbol_visibility: 'hidden', 
  link_with: [ common ],
  install: false
)

